#include "GameState.h"

// ReShade add-on API (https://github.com/crosire/reshade, BSD 3-Clause), headers of ReShade 6.8.0.
// Kept in its own file: it pulls in <windows.h>, whose A/W macros shadow CommonLibSSE members.
#include "reshade/reshade.hpp"
#undef GetObject
#undef GetClassName
#undef GetMessage

namespace {
    // Climate times are stored in ten-minute steps.
    float Hours(std::uint8_t tenMinutes) { return static_cast<float>(tenMinutes) / 6.0f; }

    float NightLevel(const RE::Sky &sky) {
        if (!sky.currentClimate) {
            return 0.0f;
        }
        const auto &timing = sky.currentClimate->timing;
        const float hour = sky.currentGameHour;
        const float sunriseBegin = Hours(timing.sunrise.begin);
        const float sunriseEnd = Hours(timing.sunrise.end);
        const float sunsetBegin = Hours(timing.sunset.begin);
        const float sunsetEnd = Hours(timing.sunset.end);
        if (hour <= sunriseBegin || hour >= sunsetEnd) {
            return 1.0f;
        }
        if (hour < sunriseEnd) {
            return 1.0f - (hour - sunriseBegin) / std::max(0.01f, sunriseEnd - sunriseBegin);
        }
        if (hour > sunsetBegin) {
            return (hour - sunsetBegin) / std::max(0.01f, sunsetEnd - sunsetBegin);
        }
        return 0.0f;
    }

    // Eases `shown` towards `target`, taking about `seconds` to get most of the way.
    float Ease(float shown, float target, float deltaSeconds, float seconds) {
        const float step = 1.0f - std::exp(-deltaSeconds / seconds);
        return shown + (target - shown) * step;
    }

    // Called by ReShade every frame, just before it draws its effects.
    void OnBeginEffects(reshade::api::effect_runtime *runtime, reshade::api::command_list *,
                        reshade::api::resource_view, reshade::api::resource_view) {
        static auto last = std::chrono::steady_clock::now();
        static float shownRain = 0.0f;

        const auto now = std::chrono::steady_clock::now();
        const float delta = std::min(0.25f, std::chrono::duration<float>(now - last).count());
        last = now;

        // the lens gets wet over a few seconds and dries quickly once the rain stops
        const float targetRain = GameState::rain;
        shownRain = Ease(shownRain, targetRain, delta, targetRain > shownRain ? 3.0f : 1.5f);

        const float interior = GameState::interior;
        const float night = GameState::night;

        // Shaders ask for a value with an annotation: uniform float X < source = "skyrok_rain"; >;
        runtime->enumerate_uniform_variables(
            nullptr, [&](reshade::api::effect_runtime *rt, reshade::api::effect_uniform_variable variable) {
                char source[32] = "";
                if (!rt->get_annotation_string_from_uniform_variable(variable, "source", source)) {
                    return;
                }
                const std::string_view name = source;
                if (name == "skyrok_rain") {
                    rt->set_uniform_value_float(variable, &shownRain, 1);
                } else if (name == "skyrok_interior") {
                    rt->set_uniform_value_float(variable, &interior, 1);
                } else if (name == "skyrok_night") {
                    rt->set_uniform_value_float(variable, &night, 1);
                }
            });
    }
}

void GameState::Update() {
    const auto *player = RE::PlayerCharacter::GetSingleton();
    const auto *cell = player ? player->GetParentCell() : nullptr;
    const auto *sky = RE::Sky::GetSingleton();
    if (!cell || !sky) {
        rain = 0.0f;
        return;
    }

    const bool inside = cell->IsInteriorCell();
    interior = inside ? 1.0f : 0.0f;
    night = NightLevel(*sky);

    if (inside || sky->mode != RE::Sky::Mode::kFull) {
        rain = 0.0f;
        return;
    }
    // weathers blend into each other: follow the blend so the lens does not get wet all at once
    const float blend = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
    const float to = RainIntensity(sky->currentWeather);
    const float from = sky->lastWeather ? RainIntensity(sky->lastWeather) : to;
    rain = from + (to - from) * blend;
}

// Shown in ReShade's add-on list.
extern "C" __declspec(dllexport) const char *NAME = "Skyrok";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Tells Skyrok's shaders what the game is doing: rain, interior or exterior, day or night.";

bool ReShadeLink::Connect() {
    // ReShade needs the handle of this dll; it finds its own module by itself.
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ReShadeLink::Connect), &self)) {
        return false;
    }
    if (!reshade::register_addon(self)) {
        return false;
    }
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnBeginEffects);
    return true;
}
