#include "GameState.h"
#include "Plugin.h"
#include "Settings.h"
#include <spdlog/sinks/basic_file_sink.h>

// SDK header of SKSE Menu Framework (https://github.com/Thiago099/SKSE-Menu-Framework-2-Example).
// It pulls in <windows.h>, whose A/W macros shadow CommonLibSSE members.
#include "SKSEMenuFramework.h"
#undef GetObject
#undef GetClassName
#undef GetMessage

// Template source: https://github.com/SkyrimDev/HelloWorld-using-CommonLibSSE-NG

namespace {
    using ColorTime = RE::TESWeather::ColorTime;
    using ColorType = RE::TESWeather::ColorType;
    using DirectionalAmbient = RE::BGSDirectionalAmbientLightingColors;

    NightSettings nights;
    MoonSettings moons;
    InteriorSettings interiors;
    ImageSpaceSettings imageSpaces;
    RainSettings rains;
    SnowSettings snows;

    // The untouched game values, kept so settings can be changed in game: every Apply* starts
    // again from these instead of stacking a new change on top of the previous one.
    struct WeatherOriginal {
        RE::TESWeather *weather;
        bool darken;   // not on the night darkness exclusion list
        bool moonlit;  // not on the moonlight exclusion list
        RE::Color colors[ColorType::kTotal][ColorTime::kTotal];
        RE::Color clouds[RE::TESWeather::kTotalLayers][ColorTime::kTotal];
        DirectionalAmbient directionalAmbient[ColorTime::kTotal];
    };
    struct TemplateOriginal {
        RE::BGSLightingTemplate *lightingTemplate;
        RE::INTERIOR_DATA data;
        DirectionalAmbient directionalAmbient;
    };
    struct CellOriginal {
        RE::TESObjectCELL *cell;
        RE::INTERIOR_DATA data;
    };
    struct ImageSpaceOriginal {
        RE::TESImageSpace *imageSpace;
        RE::ImageSpaceBaseData::Cinematic cinematic;
        float eyeAdaptSpeed;
        float eyeAdaptStrength;
    };

    using ParticleData = RE::BGSShaderParticleGeometryData::DataID;
    struct PrecipitationOriginal {
        RE::BGSShaderParticleGeometryData *particles;
        RE::BGSShaderParticleGeometryData::SETTING_VALUE values[static_cast<std::size_t>(ParticleData::kTotal)];

        bool IsRain() const {
            using Type = RE::BGSShaderParticleGeometryData::ParticleType;
            return values[static_cast<std::size_t>(ParticleData::kParticleType)].i ==
                   static_cast<std::uint32_t>(Type::kRain);
        }
    };

    std::vector<PrecipitationOriginal> precipitationOriginals;
    std::vector<WeatherOriginal> weatherOriginals;
    std::vector<TemplateOriginal> templateOriginals;
    std::vector<CellOriginal> cellOriginals;
    std::vector<ImageSpaceOriginal> imageSpaceOriginals;

    void SetupLog() {
        auto logsFolder = SKSE::log::log_directory();
        if (!logsFolder) {
            SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
            return;
        }

        auto logFilePath = *logsFolder / std::format("{}.log", Plugin::NAME);
        auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
        auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
        spdlog::set_default_logger(std::move(loggerPtr));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::info);
    }

    void Scale(RE::Color &color, float factor) {
        const auto channel = [factor](std::uint8_t value) {
            return static_cast<std::uint8_t>(std::clamp(std::lround(value * factor), 0l, 255l));
        };
        color.red = channel(color.red);
        color.green = channel(color.green);
        color.blue = channel(color.blue);
    }

    void ScaleDirectionalAmbient(DirectionalAmbient &colors, float factor) {
        auto &d = colors.directional;
        for (auto *color : {&d.x.max, &d.x.min, &d.y.max, &d.y.min, &d.z.max, &d.z.min}) {
            Scale(*color, factor);
        }
    }

    std::unordered_set<RE::FormID> Resolve(RE::TESDataHandler &dataHandler, const FormList &forms) {
        std::unordered_set<RE::FormID> resolved;
        for (const auto &[plugin, localID] : forms) {
            if (const auto formID = dataHandler.LookupFormID(localID, plugin); formID != 0) {
                resolved.insert(formID);
            }
        }
        return resolved;
    }

    // ---------------------------------------------------------------- nights

    void ScaleClouds(RE::TESWeather &weather, ColorTime time, float factor) {
        for (auto &layer : weather.cloudColorData) {
            Scale(layer[time], factor);
        }
    }

    // ---- moonlit nights

    // Light given by each of the eight moon phases, from 0 (new moon) to 1 (full moon).
    constexpr float moonLightByPhase[RE::Moon::Phase::kTotal] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.25f, 0.5f, 0.75f};
    constexpr const char *moonPhaseNames[RE::Moon::Phase::kTotal] = {
        "Full moon", "Waning gibbous", "Waning quarter", "Waning crescent",
        "New moon",  "Waxing crescent", "Waxing quarter", "Waxing gibbous"};

    std::atomic<std::uint32_t> moonPhase{RE::Moon::Phase::kWaningQuarter};  // a half moon until the game says otherwise

    // The moons advance one phase every few game days (three in vanilla), starting from a full moon.
    std::uint32_t CurrentMoonPhase() {
        const auto *calendar = RE::Calendar::GetSingleton();
        const auto *sky = RE::Sky::GetSingleton();
        if (!calendar) {
            return moonPhase;
        }
        std::uint32_t phaseLength = 3;
        if (sky && sky->currentClimate) {
            if (const auto length = sky->currentClimate->timing.GetPhaseLength(); length > 0) {
                phaseLength = length;
            }
        }
        const auto days = static_cast<std::uint32_t>(std::max(0.0f, calendar->GetDaysPassed()));
        return (days / phaseLength) % RE::Moon::Phase::kTotal;
    }

    // How much of the moonlight reaches the ground under this weather.
    float MoonVisibility(const RE::TESWeather &weather) {
        using Flag = RE::TESWeather::WeatherDataFlag;
        const auto &flags = weather.data.flags;
        if (flags.any(Flag::kRainy, Flag::kSnow)) {
            return 0.25f;
        }
        if (flags.any(Flag::kPleasant)) {
            return 1.0f;
        }
        return 0.5f;  // cloudy, or not classified
    }

    // Night darkness and moonlight are two separate features acting on the same night colours:
    // `darken` applies the darkness multipliers, `moonlit` makes the result follow the moon phase.
    // Either can be used without the other, so moonlight also works on vanilla nights.
    void ChangeNight(RE::TESWeather &weather, bool darken, bool moonlit) {
        auto &colors = weather.colorData;
        const auto night = ColorTime::kNight;

        // -1 on a new moon, +1 on a full moon, scaled down under clouds
        const float swing = moonlit ? moons.strength * MoonVisibility(weather) *
                                          (2.0f * moonLightByPhase[moonPhase] - 1.0f)
                                    : 0.0f;
        const float moon = 1.0f + swing;
        // dark nights show a few more stars, bright moons wash some out
        const float stars = 1.0f - 0.5f * swing;

        const auto factor = [darken, moon](float darkness) { return (darken ? darkness : 1.0f) * moon; };

        Scale(colors[ColorType::kSkyUpper][night], factor(nights.sky));
        Scale(colors[ColorType::kSkyLower][night], factor(nights.sky));
        Scale(colors[ColorType::kHorizon][night], factor(nights.sky));
        Scale(colors[ColorType::kFogNear][night], factor(nights.fog));
        Scale(colors[ColorType::kFogFar][night], factor(nights.fog));
        Scale(colors[ColorType::kAmbient][night], factor(nights.ambient));
        Scale(colors[ColorType::kSunlight][night], factor(nights.sunlight));
        Scale(colors[ColorType::kEffectLighting][night], factor(nights.effectLighting));
        Scale(colors[ColorType::kSkyStatics][night], factor(nights.skyStatics));
        Scale(colors[ColorType::kWaterMultiplier][night], factor(nights.water));
        Scale(colors[ColorType::kStars][night], (darken ? nights.stars : 1.0f) * stars);
        ScaleDirectionalAmbient(weather.directionalAmbientLightingColors[night], factor(nights.ambient));
        ScaleClouds(weather, night, factor(nights.clouds));

        if (darken) {
            for (const auto twilight : {ColorTime::kSunrise, ColorTime::kSunset}) {
                Scale(colors[ColorType::kAmbient][twilight], nights.twilightAmbient);
                ScaleDirectionalAmbient(weather.directionalAmbientLightingColors[twilight], nights.twilightAmbient);
                ScaleClouds(weather, twilight, nights.twilightClouds);
            }
        }
    }

    // Weather mods' records are already in memory when this runs, so whatever weathers
    // the load order ends up with get the same treatment.
    void SnapshotWeathers(RE::TESDataHandler &dataHandler) {
        const auto noDarkness = Resolve(dataHandler, nights.excluded);
        const auto noMoonlight = Resolve(dataHandler, moons.excluded);
        std::size_t darkened = 0;
        std::size_t moonlit = 0;
        for (auto *weather : dataHandler.GetFormArray<RE::TESWeather>()) {
            if (!weather) {
                continue;
            }
            auto &original = weatherOriginals.emplace_back();
            original.weather = weather;
            original.darken = !noDarkness.contains(weather->GetFormID());
            original.moonlit = !noMoonlight.contains(weather->GetFormID());
            darkened += original.darken;
            moonlit += original.moonlit;
            std::memcpy(original.colors, weather->colorData, sizeof(original.colors));
            std::memcpy(original.clouds, weather->cloudColorData, sizeof(original.clouds));
            std::memcpy(original.directionalAmbient, weather->directionalAmbientLightingColors,
                        sizeof(original.directionalAmbient));
        }
        SKSE::log::info("Weathers: {} in total, {} open to night darkness, {} open to moonlight",
                        weatherOriginals.size(), darkened, moonlit);
    }

    void ApplyNights() {
        for (const auto &original : weatherOriginals) {
            auto &weather = *original.weather;
            std::memcpy(weather.colorData, original.colors, sizeof(original.colors));
            std::memcpy(weather.cloudColorData, original.clouds, sizeof(original.clouds));
            std::memcpy(weather.directionalAmbientLightingColors, original.directionalAmbient,
                        sizeof(original.directionalAmbient));
            const bool darken = nights.enabled && original.darken;
            const bool moonlit = moons.enabled && original.moonlit;
            if (darken || moonlit) {
                ChangeNight(weather, darken, moonlit);
            }
        }
    }

    // Re-applies the night colours when the moon enters a new phase. Runs on the game's main thread.
    void CheckMoon() {
        const auto phase = CurrentMoonPhase();
        if (phase == moonPhase) {
            return;
        }
        moonPhase = phase;
        if (moons.enabled) {
            ApplyNights();
        }
    }

    // The phase only changes every few game days, but waiting, sleeping and fast travel can skip
    // through several at once, so it is simply checked every few seconds.
    // The same timer keeps the state published to ReShade fresh, four times a second.
    void WatchGame() {
        std::thread([] {
            for (std::uint32_t tick = 0;; ++tick) {
                std::this_thread::sleep_for(250ms);
                SKSE::GetTaskInterface()->AddTask(GameState::Update);
                if (tick % 20 == 0) {
                    SKSE::GetTaskInterface()->AddTask(CheckMoon);
                }
            }
        }).detach();
    }

    // ---------------------------------------------------------------- interiors

    void Darken(RE::INTERIOR_DATA &lighting) {
        Scale(lighting.ambient, interiors.ambient);
        ScaleDirectionalAmbient(lighting.directionalAmbientLightingColors, interiors.ambient);
        Scale(lighting.directional, interiors.directional);
        Scale(lighting.fogColorNear, interiors.fog);
        Scale(lighting.fogColorFar, interiors.fog);
        if (interiors.lightFadeEndMin > 0.0f) {
            lighting.lightFadeStart = std::max(lighting.lightFadeStart, interiors.lightFadeStartMin);
            lighting.lightFadeEnd = std::max(lighting.lightFadeEnd, interiors.lightFadeEndMin);
        }
    }

    // A cell either inherits a value from its lighting template or uses its own, so templates
    // and cells are both handled, each exactly once: every interior ends up darkened the same way.
    void SnapshotInteriors(RE::TESDataHandler &dataHandler) {
        const auto excluded = Resolve(dataHandler, interiors.excluded);

        std::size_t skippedTemplates = 0;
        for (auto *lightingTemplate : dataHandler.GetFormArray<RE::BGSLightingTemplate>()) {
            if (!lightingTemplate) {
                continue;
            }
            if (excluded.contains(lightingTemplate->GetFormID())) {
                ++skippedTemplates;
                continue;
            }
            templateOriginals.push_back(
                {lightingTemplate, lightingTemplate->data, lightingTemplate->directionalAmbientLightingColors});
        }

        std::size_t skippedCells = 0;
        std::size_t withoutLighting = 0;
        {
            const auto &[forms, lock] = RE::TESForm::GetAllForms();
            const RE::BSReadLockGuard guard{lock};
            for (const auto &[formID, form] : *forms) {
                auto *cell = form ? form->As<RE::TESObjectCELL>() : nullptr;
                if (!cell || !cell->IsInteriorCell()) {
                    continue;
                }
                const auto *cellTemplate = cell->GetRuntimeData().lightingTemplate;
                if (cellTemplate && excluded.contains(cellTemplate->GetFormID())) {
                    ++skippedCells;
                    continue;
                }
                if (const auto *lighting = cell->GetLighting()) {
                    cellOriginals.push_back({cell, *lighting});
                } else {
                    ++withoutLighting;
                }
            }
        }
        SKSE::log::info("Lighting templates handled: {}, excluded: {}", templateOriginals.size(), skippedTemplates);
        SKSE::log::info("Interior cells handled: {}, excluded: {}, without lighting data: {}", cellOriginals.size(),
                        skippedCells, withoutLighting);
    }

    void ApplyInteriors() {
        for (const auto &original : templateOriginals) {
            auto &lightingTemplate = *original.lightingTemplate;
            lightingTemplate.data = original.data;
            lightingTemplate.directionalAmbientLightingColors = original.directionalAmbient;
            if (interiors.enabled) {
                Darken(lightingTemplate.data);
                // templates keep a second copy of the directional ambient colours (the DALC record field)
                ScaleDirectionalAmbient(lightingTemplate.directionalAmbientLightingColors, interiors.ambient);
            }
        }
        for (const auto &original : cellOriginals) {
            auto *lighting = original.cell->GetLighting();
            if (!lighting) {
                continue;
            }
            *lighting = original.data;
            if (interiors.enabled) {
                Darken(*lighting);
            }
        }
    }

    // ---------------------------------------------------------------- image spaces

    // The game desaturates in two steps: the cinematic saturation, then a tint that blends the
    // picture towards (luminance x tint colour). Vanilla pairs a strong tint with a high saturation,
    // so writing a plain saturation value leaves the picture washed out wherever the tint is strong
    // (most exteriors, nights above all). This returns the saturation that gives `target` on screen.
    float SaturationFor(float target, const RE::ImageSpaceBaseData::Tint &tint) {
        constexpr float maxSaturation = 3.0f;
        const float amount = std::clamp(tint.amount, 0.0f, 1.0f);
        const float kept = 1.0f - amount;  // share of the original colour that survives the tint
        if (kept < 0.05f) {
            return -1.0f;  // fully tinted on purpose (monochrome effects): leave it alone
        }
        const float tintLuminance = 0.2125f * tint.color.red + 0.7154f * tint.color.green + 0.0721f * tint.color.blue;
        const float luminanceFactor = kept + amount * tintLuminance;
        return std::min(maxSaturation, target * luminanceFactor / kept);
    }

    void SnapshotImageSpaces(RE::TESDataHandler &dataHandler) {
        const auto excluded = Resolve(dataHandler, imageSpaces.excluded);
        std::size_t skipped = 0;
        for (auto *imageSpace : dataHandler.GetFormArray<RE::TESImageSpace>()) {
            if (!imageSpace) {
                continue;
            }
            if (excluded.contains(imageSpace->GetFormID())) {
                ++skipped;
                continue;
            }
            const auto &data = imageSpace->data;
            imageSpaceOriginals.push_back({imageSpace, data.cinematic, data.hdr.eyeAdaptSpeed, data.hdr.eyeAdaptStrength});
        }
        SKSE::log::info("Image spaces handled: {}, excluded: {}", imageSpaceOriginals.size(), skipped);
    }

    void ApplyImageSpaces() {
        for (const auto &original : imageSpaceOriginals) {
            auto &data = original.imageSpace->data;
            data.cinematic = original.cinematic;
            data.hdr.eyeAdaptSpeed = original.eyeAdaptSpeed;
            data.hdr.eyeAdaptStrength = original.eyeAdaptStrength;
            if (!imageSpaces.enabled) {
                continue;
            }
            if (imageSpaces.saturation > 0.0f) {
                if (const float saturation = SaturationFor(imageSpaces.saturation, data.tint); saturation >= 0.0f) {
                    data.cinematic.saturation = saturation;
                }
            }
            if (imageSpaces.brightness > 0.0f) {
                data.cinematic.brightness = imageSpaces.brightness;
            }
            if (imageSpaces.contrast > 0.0f) {
                data.cinematic.contrast = imageSpaces.contrast;
            }
            if (imageSpaces.eyeAdaptSpeed > 0.0f) {
                data.hdr.eyeAdaptSpeed = imageSpaces.eyeAdaptSpeed;
            }
            if (imageSpaces.eyeAdaptStrength > 0.0f) {
                data.hdr.eyeAdaptStrength = imageSpaces.eyeAdaptStrength;
            }
        }
    }

    // ---------------------------------------------------------------- rain and snow

    // Only particle systems used as precipitation by a rainy or snowy weather are handled, so
    // look-alikes (blowing grass, magic effects, stardust) and unused records are left alone,
    // while rain and snow added by other weather mods are picked up.
    void SnapshotPrecipitation(RE::TESDataHandler &dataHandler) {
        using Flag = RE::TESWeather::WeatherDataFlag;
        std::unordered_set<RE::BGSShaderParticleGeometryData *> seen;
        std::size_t rain = 0;
        std::size_t snow = 0;
        for (auto *weather : dataHandler.GetFormArray<RE::TESWeather>()) {
            auto *particles = weather ? weather->precipitationData : nullptr;
            if (!particles || !weather->data.flags.any(Flag::kRainy, Flag::kSnow) || !seen.insert(particles).second) {
                continue;
            }
            auto &original = precipitationOriginals.emplace_back();
            original.particles = particles;
            for (std::uint32_t id = 0; id < std::size(original.values); ++id) {
                original.values[id] = particles->GetSettingValue(static_cast<ParticleData>(id));
            }
            (original.IsRain() ? rain : snow) += 1;
        }
        SKSE::log::info("Precipitation handled: {} rain, {} snow", rain, snow);
    }

    void ApplyPrecipitation() {
        for (const auto &original : precipitationOriginals) {
            auto &particles = *original.particles;
            for (std::uint32_t id = 0; id < std::size(original.values); ++id) {
                particles.GetSettingRef(static_cast<ParticleData>(id)) = original.values[id];
            }
            const auto scale = [&particles](ParticleData id, float factor) {
                auto &value = particles.GetSettingRef(id);
                if (id == ParticleData::kBoxSize) {
                    // the box size is the one whole-number setting in the record
                    value.i = static_cast<std::uint32_t>(std::max(1l, std::lround(value.i * factor)));
                } else {
                    value.f *= factor;
                }
            };
            if (original.IsRain() && rains.enabled) {
                scale(ParticleData::kParticleDensity, rains.density);
                scale(ParticleData::kGravityVelocity, rains.speed);
                scale(ParticleData::kParticleSizeX, rains.width);
                scale(ParticleData::kParticleSizeY, rains.length);
                scale(ParticleData::kBoxSize, rains.area);
            } else if (!original.IsRain() && snows.enabled) {
                scale(ParticleData::kParticleDensity, snows.density);
                scale(ParticleData::kGravityVelocity, snows.speed);
                scale(ParticleData::kRotationVelocity, snows.speed);
                scale(ParticleData::kParticleSizeX, snows.size);
                scale(ParticleData::kParticleSizeY, snows.size);
                scale(ParticleData::kBoxSize, snows.area);
            }
        }
    }

}

float GameState::RainIntensity(const RE::TESWeather *weather) {
    if (!weather || !weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
        return 0.0f;
    }
    // Vanilla rain has a particle density of 1 and storm rain 2; other mods are placed on the same scale.
    for (const auto &original : precipitationOriginals) {
        if (original.particles == weather->precipitationData) {
            const float density = original.values[static_cast<std::size_t>(ParticleData::kParticleDensity)].f;
            return std::clamp(0.5f * density, 0.35f, 1.0f);
        }
    }
    return 0.5f;  // rainy without rain particles of its own
}

namespace {
    // ---------------------------------------------------------------- in-game menu

    bool Factor(const char *label, float *value) { return ImGui::SliderFloat(label, value, 0.0f, 2.0f, "%.2f"); }

    // Presets offered as buttons. Same values as the ini files shipped by the installer
    // (dist/make_ini.py): keep the two in step.
    struct NightPreset {
        const char *name;
        float sky, fog, ambient, sunlight, effectLighting, skyStatics, water, stars, clouds;
        float twilightAmbient, twilightClouds;
    };
    constexpr NightPreset nightPresets[] = {
        {"Vanilla", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {"Darker", 0.56f, 0.49f, 0.44f, 0.33f, 0.36f, 0.58f, 0.33f, 0.80f, 0.57f, 0.73f, 0.83f},
        {"Super darker", 0.42f, 0.30f, 0.25f, 0.10f, 0.15f, 0.43f, 0.10f, 0.80f, 0.42f, 0.64f, 0.77f},
    };

    struct InteriorPreset {
        const char *name;
        float ambient, directional, fog, lightFadeStartMin, lightFadeEndMin;
    };
    constexpr InteriorPreset interiorPresets[] = {
        {"Vanilla", 1.0f, 1.0f, 1.0f, 0.0f, 0.0f},
        {"Darker", 0.48f, 0.22f, 0.40f, 15000.0f, 20000.0f},
        {"Super darker", 0.30f, 0.10f, 0.20f, 15000.0f, 20000.0f},
    };

    struct ImageSpacePreset {
        const char *name;
        float saturation, brightness, contrast, eyeAdaptSpeed, eyeAdaptStrength;
    };
    constexpr ImageSpacePreset imageSpacePresets[] = {
        {"Vanilla", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {"Optimistic", 0.70f, 1.1f, 1.2f, 80.0f, 1.0f},
        {"Realistic", 0.63f, 1.0f, 1.3f, 80.0f, 1.0f},
        {"Neutral", 0.70f, 1.0f, 1.0f, 80.0f, 1.0f},
        {"Bright", 0.84f, 1.2f, 1.2f, 80.0f, 1.0f},
        {"Dark", 0.56f, 0.8f, 1.2f, 80.0f, 1.0f},
    };

    // One button per preset on a single row. Returns the clicked preset, if any.
    template <class T, std::size_t N>
    const T *PresetButtons(const T (&presets)[N]) {
        const T *clicked = nullptr;
        ImGui::SeparatorText("Presets");
        for (std::size_t i = 0; i < N; ++i) {
            if (i > 0) {
                ImGui::SameLine();
            }
            if (ImGui::Button(presets[i].name)) {
                clicked = &presets[i];
            }
        }
        return clicked;
    }

    // Save / reload row shared by the three pages. Returns true when the settings were reloaded.
    template <class T>
    bool FileButtons(T &settings) {
        static std::string status;
        bool reloaded = false;
        ImGui::Spacing();
        if (ImGui::Button("Save to ini")) {
            status = settings.Save() ? std::format("Saved to {}", T::FILE) : std::format("Could not write {}", T::FILE);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload from ini")) {
            reloaded = settings.Load();
            status = reloaded ? std::format("Reloaded {}", T::FILE) : std::format("{} not found", T::FILE);
        }
        if (!status.empty()) {
            ImGui::TextDisabled("%s", status.c_str());
        }
        return reloaded;
    }

    void __stdcall RenderNights() {
        ImGui::TextWrapped("Multipliers applied to the night colours of every weather. 1.00 = unchanged, lower = darker.");
        bool changed = ImGui::Checkbox("Enabled", &nights.enabled);
        if (const auto *preset = PresetButtons(nightPresets)) {
            nights.enabled = true;
            nights.sky = preset->sky;
            nights.fog = preset->fog;
            nights.ambient = preset->ambient;
            nights.sunlight = preset->sunlight;
            nights.effectLighting = preset->effectLighting;
            nights.skyStatics = preset->skyStatics;
            nights.water = preset->water;
            nights.stars = preset->stars;
            nights.clouds = preset->clouds;
            nights.twilightAmbient = preset->twilightAmbient;
            nights.twilightClouds = preset->twilightClouds;
            changed = true;
        }
        ImGui::SeparatorText("Night");
        changed |= Factor("Sky", &nights.sky);
        changed |= Factor("Fog", &nights.fog);
        changed |= Factor("Ambient", &nights.ambient);
        changed |= Factor("Sunlight (moonlight)", &nights.sunlight);
        changed |= Factor("Effect lighting", &nights.effectLighting);
        changed |= Factor("Sky statics", &nights.skyStatics);
        changed |= Factor("Water", &nights.water);
        changed |= Factor("Stars", &nights.stars);
        changed |= Factor("Clouds", &nights.clouds);
        ImGui::SeparatorText("Sunrise and sunset");
        changed |= Factor("Ambient##twilight", &nights.twilightAmbient);
        changed |= Factor("Clouds##twilight", &nights.twilightClouds);
        changed |= FileButtons(nights);
        if (changed) {
            ApplyNights();
        }
    }

    struct MoonPreset {
        const char *name;
        float strength;
    };
    constexpr MoonPreset moonPresets[] = {
        {"Subtle", 0.15f},
        {"Normal", 0.30f},
        {"Strong", 0.50f},
    };

    void __stdcall RenderMoonlight() {
        ImGui::TextWrapped("Nights follow the moons: brighter under a full moon, darker on a new moon, less so "
                           "under clouds, rain or snow. A half moon leaves the night as it is. "
                           "Works on vanilla nights and together with Skyrok's darker nights.");
        bool changed = ImGui::Checkbox("Enabled", &moons.enabled);
        if (const auto *preset = PresetButtons(moonPresets)) {
            moons.enabled = true;
            moons.strength = preset->strength;
            changed = true;
        }
        ImGui::SeparatorText("Moonlight");
        changed |= ImGui::SliderFloat("Strength", &moons.strength, 0.0f, 1.0f, "%.2f");
        {
            const std::uint32_t phase = moonPhase;
            const float swing = moons.strength * (2.0f * moonLightByPhase[phase] - 1.0f) * 100.0f;
            ImGui::TextDisabled("Tonight: %s. Clear nights are %+.0f%% as bright.", moonPhaseNames[phase], swing);
        }
        changed |= FileButtons(moons);
        if (changed) {
            ApplyNights();
        }
    }

    void __stdcall RenderInteriors() {
        ImGui::TextWrapped("Multipliers applied to every interior. 1.00 = unchanged, lower = darker. "
                           "Leave and re-enter the place if a change does not show.");
        bool changed = ImGui::Checkbox("Enabled", &interiors.enabled);
        if (const auto *preset = PresetButtons(interiorPresets)) {
            interiors.enabled = true;
            interiors.ambient = preset->ambient;
            interiors.directional = preset->directional;
            interiors.fog = preset->fog;
            interiors.lightFadeStartMin = preset->lightFadeStartMin;
            interiors.lightFadeEndMin = preset->lightFadeEndMin;
            changed = true;
        }
        ImGui::SeparatorText("Interior");
        changed |= Factor("Ambient", &interiors.ambient);
        changed |= Factor("Directional light", &interiors.directional);
        changed |= Factor("Fog", &interiors.fog);
        ImGui::SeparatorText("Light fade distances (0 = unchanged)");
        changed |= ImGui::SliderFloat("Start at least", &interiors.lightFadeStartMin, 0.0f, 30000.0f, "%.0f");
        changed |= ImGui::SliderFloat("End at least", &interiors.lightFadeEndMin, 0.0f, 30000.0f, "%.0f");
        changed |= FileButtons(interiors);
        if (changed) {
            ApplyInteriors();
        }
    }

    void __stdcall RenderImageSpaces() {
        ImGui::TextWrapped("The same grading for every image space. 0 = leave the game's value. "
                           "A weather or location change may be needed before a change shows.");
        bool changed = ImGui::Checkbox("Enabled", &imageSpaces.enabled);
        if (const auto *preset = PresetButtons(imageSpacePresets)) {
            imageSpaces.enabled = true;
            imageSpaces.saturation = preset->saturation;
            imageSpaces.brightness = preset->brightness;
            imageSpaces.contrast = preset->contrast;
            imageSpaces.eyeAdaptSpeed = preset->eyeAdaptSpeed;
            imageSpaces.eyeAdaptStrength = preset->eyeAdaptStrength;
            changed = true;
        }
        ImGui::SeparatorText("Colour");
        changed |= ImGui::SliderFloat("Saturation (on screen)", &imageSpaces.saturation, 0.0f, 1.5f, "%.2f");
        changed |= ImGui::SliderFloat("Brightness", &imageSpaces.brightness, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Contrast", &imageSpaces.contrast, 0.0f, 2.0f, "%.2f");
        ImGui::SeparatorText("Eye adaptation");
        changed |= ImGui::SliderFloat("Speed", &imageSpaces.eyeAdaptSpeed, 0.0f, 200.0f, "%.0f");
        changed |= ImGui::SliderFloat("Strength", &imageSpaces.eyeAdaptStrength, 0.0f, 10.0f, "%.1f");
        changed |= FileButtons(imageSpaces);
        if (changed) {
            ApplyImageSpaces();
        }
    }

    struct RainPreset {
        const char *name;
        float density, speed, width, length, area;
    };
    constexpr RainPreset rainPresets[] = {
        {"Vanilla", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {"Heavy", 8.0f, 1.2f, 1.15f, 0.5f, 1.1f},
    };

    struct SnowPreset {
        const char *name;
        float density, speed, size, area;
    };
    constexpr SnowPreset snowPresets[] = {
        {"Vanilla", 1.0f, 1.0f, 1.0f, 1.0f},
        {"Heavy", 2.5f, 0.8f, 1.15f, 1.3f},
    };

    void __stdcall RenderPrecipitation() {
        ImGui::TextWrapped("Multipliers applied to the rain and snow of every weather. 1.00 = unchanged. "
                           "Changes show the next time it starts raining or snowing.");
        bool changed = false;

        // rain and snow share labels (Enabled, Vanilla, Density...), so each half gets its own id scope
        ImGui::PushID("rain");
        ImGui::SeparatorText("Rain");
        changed |= ImGui::Checkbox("Enabled", &rains.enabled);
        if (const auto *preset = PresetButtons(rainPresets)) {
            rains.enabled = true;
            rains.density = preset->density;
            rains.speed = preset->speed;
            rains.width = preset->width;
            rains.length = preset->length;
            rains.area = preset->area;
            changed = true;
        }
        changed |= ImGui::SliderFloat("Density", &rains.density, 0.0f, 20.0f, "%.1f");
        changed |= ImGui::SliderFloat("Fall speed", &rains.speed, 0.1f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Drop width", &rains.width, 0.1f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Drop length", &rains.length, 0.1f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Area", &rains.area, 0.5f, 2.0f, "%.2f");
        changed |= FileButtons(rains);
        ImGui::PopID();

        ImGui::PushID("snow");
        ImGui::SeparatorText("Snow");
        changed |= ImGui::Checkbox("Enabled", &snows.enabled);
        if (const auto *preset = PresetButtons(snowPresets)) {
            snows.enabled = true;
            snows.density = preset->density;
            snows.speed = preset->speed;
            snows.size = preset->size;
            snows.area = preset->area;
            changed = true;
        }
        changed |= ImGui::SliderFloat("Density", &snows.density, 0.0f, 10.0f, "%.1f");
        changed |= ImGui::SliderFloat("Fall speed", &snows.speed, 0.1f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Flake size", &snows.size, 0.1f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Area", &snows.area, 0.5f, 2.0f, "%.2f");
        changed |= FileButtons(snows);
        ImGui::PopID();

        if (changed) {
            ApplyPrecipitation();
        }
    }

    void RegisterMenu() {
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::info("SKSE Menu Framework not installed: settings come from the ini files only.");
            return;
        }
        SKSEMenuFramework::SetSection("Skyrok");
        SKSEMenuFramework::AddSectionItem("Nights", RenderNights);
        SKSEMenuFramework::AddSectionItem("Moonlit nights", RenderMoonlight);
        SKSEMenuFramework::AddSectionItem("Interiors", RenderInteriors);
        SKSEMenuFramework::AddSectionItem("Image Space", RenderImageSpaces);
        SKSEMenuFramework::AddSectionItem("Rain and Snow", RenderPrecipitation);
        SKSE::log::info("In-game menu registered with SKSE Menu Framework.");
    }

    // Skyrok's shaders carry their version in a comment near the top: "// Version: 1.2".
    // Returns the numbers of that line, or nothing for a file without one (older than any version).
    std::vector<int> ShaderVersion(const std::filesystem::path &path) {
        std::ifstream file(path);
        std::string line;
        for (int i = 0; i < 20 && std::getline(file, line); ++i) {
            const auto at = line.find("Version:");
            if (at == std::string::npos) {
                continue;
            }
            std::vector<int> version;
            int number = -1;
            for (const char c : line.substr(at + 8)) {
                if (c >= '0' && c <= '9') {
                    number = std::max(number, 0) * 10 + (c - '0');
                } else if (number >= 0) {
                    version.push_back(number);
                    number = -1;
                    if (c != '.') {
                        break;
                    }
                }
            }
            if (number >= 0) {
                version.push_back(number);
            }
            return version;
        }
        return {};
    }

    std::string VersionText(const std::vector<int> &version) {
        std::string text;
        for (const int number : version) {
            text += (text.empty() ? "" : ".") + std::to_string(number);
        }
        return text.empty() ? "none" : text;
    }

    // Mod managers can only install into Data, and ReShade looks for shaders next to the game's exe.
    // So the installer puts Skyrok's shaders in Data and they are copied across here, at startup,
    // before ReShade loads its effects. Nothing is done when ReShade's shader folder does not exist.
    void InstallReShadeShaders() {
        namespace fs = std::filesystem;
        std::error_code error;
        const fs::path source = "Data/SKSE/Plugins/Skyrok/Shaders";
        const fs::path reshade = "reshade-shaders/Shaders";
        if (!fs::is_directory(source, error)) {
            return;  // the ReShade option was not installed
        }
        if (!fs::is_directory(reshade, error)) {
            SKSE::log::info("Skyrok's ReShade shaders are installed, but {} was not found: nothing copied.",
                            reshade.string());
            return;
        }

        const fs::path target = reshade / "Skyrok";
        fs::create_directories(target, error);
        for (const auto &entry : fs::directory_iterator(source, error)) {
            if (!entry.is_regular_file(error)) {
                continue;
            }
            const auto destination = target / entry.path().filename();
            // Copy when the shader is missing or when this Skyrok brings a newer version of it.
            // An equal or newer copy is left alone, so nothing is rewritten on a normal start.
            const auto shipped = ShaderVersion(entry.path());
            if (fs::exists(destination, error)) {
                const auto installed = ShaderVersion(destination);
                if (!(installed < shipped)) {
                    continue;
                }
            }
            fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing, error);
            if (error) {
                SKSE::log::warn("Could not copy {} to {}: {}", entry.path().filename().string(), target.string(),
                                error.message());
            } else {
                SKSE::log::info("Copied {} (version {}) to {}", entry.path().filename().string(),
                                VersionText(shipped), target.string());
            }
        }
    }

    // Runs once, after every plugin is loaded.
    void OnMessage(SKSE::MessagingInterface::Message *message) {
        if (message->type != SKSE::MessagingInterface::kDataLoaded) {
            return;
        }
        auto *dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::error("No data handler, nothing done.");
            return;
        }
        SnapshotWeathers(*dataHandler);
        SnapshotInteriors(*dataHandler);
        SnapshotImageSpaces(*dataHandler);
        SnapshotPrecipitation(*dataHandler);
        ApplyPrecipitation();
        ApplyNights();
        ApplyInteriors();
        ApplyImageSpaces();
        RegisterMenu();
        if (ReShadeLink::Connect()) {
            SKSE::log::info("Connected to ReShade: rain, interior and night values are sent to its shaders.");
        } else {
            SKSE::log::info("ReShade not found, or it does not accept add-ons: screen effects are not driven.");
        }
        WatchGame();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    // Init first: it resolves the runtime module that log_directory() needs.
    // Pass false so CommonLibSSE does not install its own logger over ours.
    SKSE::Init(skse, false);

    SetupLog();

    SKSE::log::info("{} v{}", Plugin::NAME, Plugin::VERSION);
    SKSE::log::info("Game version : {}", skse->RuntimeVersion().string());

    InstallReShadeShaders();

    // Each feature has its own ini, so the installer can offer them separately.
    // A missing ini means the feature starts disabled; it can still be switched on from the menu.
    if (nights.Load()) {
        SKSE::log::info("Nights {}: sky {} fog {} ambient {} sunlight {} effect {} statics {} water {} stars {} clouds {}"
                        " | twilight ambient {} clouds {} | exclude entries {}",
                        nights.enabled ? "on" : "off", nights.sky, nights.fog, nights.ambient, nights.sunlight,
                        nights.effectLighting, nights.skyStatics, nights.water, nights.stars, nights.clouds,
                        nights.twilightAmbient, nights.twilightClouds, nights.excluded.size());
    } else {
        SKSE::log::info("{} not found: nights are left alone.", NightSettings::FILE);
    }
    if (interiors.Load()) {
        SKSE::log::info("Interiors {}: ambient {} directional {} fog {} | light fade min {} / {} | exclude entries {}",
                        interiors.enabled ? "on" : "off", interiors.ambient, interiors.directional, interiors.fog,
                        interiors.lightFadeStartMin, interiors.lightFadeEndMin, interiors.excluded.size());
    } else {
        SKSE::log::info("{} not found: interiors are left alone.", InteriorSettings::FILE);
    }
    if (moons.Load()) {
        SKSE::log::info("Moonlit nights {}: strength {} | exclude entries {}", moons.enabled ? "on" : "off",
                        moons.strength, moons.excluded.size());
    } else {
        SKSE::log::info("{} not found: nights do not follow the moons.", MoonSettings::FILE);
    }
    if (imageSpaces.Load()) {
        SKSE::log::info("Image spaces {}: saturation {} brightness {} contrast {} | eye adapt speed {} strength {}"
                        " | exclude entries {}",
                        imageSpaces.enabled ? "on" : "off", imageSpaces.saturation, imageSpaces.brightness,
                        imageSpaces.contrast, imageSpaces.eyeAdaptSpeed, imageSpaces.eyeAdaptStrength,
                        imageSpaces.excluded.size());
    } else {
        SKSE::log::info("{} not found: image spaces are left alone.", ImageSpaceSettings::FILE);
    }

    if (rains.Load()) {
        SKSE::log::info("Rain {}: density {} speed {} width {} length {} area {}", rains.enabled ? "on" : "off",
                        rains.density, rains.speed, rains.width, rains.length, rains.area);
    } else {
        SKSE::log::info("{} not found: rain is left alone.", RainSettings::FILE);
    }
    if (snows.Load()) {
        SKSE::log::info("Snow {}: density {} speed {} size {} area {}", snows.enabled ? "on" : "off", snows.density,
                        snows.speed, snows.size, snows.area);
    } else {
        SKSE::log::info("{} not found: snow is left alone.", SnowSettings::FILE);
    }

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    return true;
}
