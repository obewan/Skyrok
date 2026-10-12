#pragma once

#include <SimpleIni.h>

// "Plugin.esm|0x0123AB" entries, resolved once the game data is loaded
using FormList = std::vector<std::pair<std::string, RE::FormID>>;

namespace IniTools {
    // negative values make no sense and values above 4 are almost certainly typos
    inline float Factor(const CSimpleIniA &ini, const char *section, const char *key, float max = 4.0f) {
        return std::clamp(static_cast<float>(ini.GetDoubleValue(section, key, 1.0)), 0.0f, max);
    }

    inline float Value(const CSimpleIniA &ini, const char *section, const char *key, float max) {
        return std::clamp(static_cast<float>(ini.GetDoubleValue(section, key, 0.0)), 0.0f, max);
    }

    inline FormList Forms(const CSimpleIniA &ini, const char *section) {
        FormList forms;
        CSimpleIniA::TNamesDepend keys;
        ini.GetAllKeys(section, keys);
        for (const auto &key : keys) {
            const std::string entry = key.pItem;
            const auto bar = entry.find('|');
            if (bar == std::string::npos) {
                SKSE::log::warn("[{}] entry ignored (expected Plugin|0xFormID): {}", section, entry);
                continue;
            }
            try {
                const auto localID = static_cast<RE::FormID>(std::stoul(entry.substr(bar + 1), nullptr, 16));
                forms.emplace_back(entry.substr(0, bar), localID);
            } catch (const std::exception &) {
                SKSE::log::warn("[{}] entry ignored (bad form id): {}", section, entry);
            }
        }
        return forms;
    }

    inline bool Open(CSimpleIniA &ini, const std::filesystem::path &path) {
        ini.SetUnicode();
        return ini.LoadFile(path.c_str()) >= 0;
    }

    // Keeps the comments and the [Exclude] list of an existing file: only the given keys are rewritten.
    inline void Set(CSimpleIniA &ini, const char *section, const char *key, float value) {
        ini.SetValue(section, key, std::format("{:.2f}", value).c_str(), nullptr, true);
    }
}

// Skyrok_Nights.ini: multipliers applied to weather colours. 1.0 leaves a colour untouched.
struct NightSettings {
    static constexpr auto FILE = "Data/SKSE/Plugins/Skyrok_Nights.ini";

    bool enabled = false;

    float sky = 1.0f;
    float fog = 1.0f;
    float ambient = 1.0f;
    float sunlight = 1.0f;
    float effectLighting = 1.0f;
    float skyStatics = 1.0f;
    float water = 1.0f;
    float stars = 1.0f;
    float clouds = 1.0f;

    // sunrise and sunset
    float twilightAmbient = 1.0f;
    float twilightClouds = 1.0f;

    // Moonlit nights: the night values above follow the moon phase. With a strength of 0.30 a
    // clear full-moon night is 30% brighter than the values above, a new-moon night 30% darker.
    bool moonlit = false;
    float moonStrength = 0.3f;

    FormList excluded;

    bool Load() {
        CSimpleIniA ini;
        if (!IniTools::Open(ini, FILE)) {
            return false;
        }
        enabled = ini.GetBoolValue("General", "Enabled", true);
        moonlit = ini.GetBoolValue("Moonlight", "Enabled", false);
        moonStrength = std::clamp(static_cast<float>(ini.GetDoubleValue("Moonlight", "Strength", 0.3)), 0.0f, 1.0f);

        sky = IniTools::Factor(ini, "Night", "Sky");
        fog = IniTools::Factor(ini, "Night", "Fog");
        ambient = IniTools::Factor(ini, "Night", "Ambient");
        sunlight = IniTools::Factor(ini, "Night", "Sunlight");
        effectLighting = IniTools::Factor(ini, "Night", "EffectLighting");
        skyStatics = IniTools::Factor(ini, "Night", "SkyStatics");
        water = IniTools::Factor(ini, "Night", "Water");
        stars = IniTools::Factor(ini, "Night", "Stars");
        clouds = IniTools::Factor(ini, "Night", "Clouds");

        twilightAmbient = IniTools::Factor(ini, "Twilight", "Ambient");
        twilightClouds = IniTools::Factor(ini, "Twilight", "Clouds");

        excluded = IniTools::Forms(ini, "Exclude");
        return true;
    }

    bool Save() const {
        CSimpleIniA ini;
        IniTools::Open(ini, FILE);
        ini.SetBoolValue("General", "Enabled", enabled, nullptr, true);
        IniTools::Set(ini, "Night", "Sky", sky);
        IniTools::Set(ini, "Night", "Fog", fog);
        IniTools::Set(ini, "Night", "Ambient", ambient);
        IniTools::Set(ini, "Night", "Sunlight", sunlight);
        IniTools::Set(ini, "Night", "EffectLighting", effectLighting);
        IniTools::Set(ini, "Night", "SkyStatics", skyStatics);
        IniTools::Set(ini, "Night", "Water", water);
        IniTools::Set(ini, "Night", "Stars", stars);
        IniTools::Set(ini, "Night", "Clouds", clouds);
        IniTools::Set(ini, "Twilight", "Ambient", twilightAmbient);
        IniTools::Set(ini, "Twilight", "Clouds", twilightClouds);
        ini.SetBoolValue("Moonlight", "Enabled", moonlit, nullptr, true);
        IniTools::Set(ini, "Moonlight", "Strength", moonStrength);
        return ini.SaveFile(FILE) >= 0;
    }
};

// Skyrok_ImageSpace.ini: the same grading applied to every image space. 0 leaves a value alone.
struct ImageSpaceSettings {
    static constexpr auto FILE = "Data/SKSE/Plugins/Skyrok_ImageSpace.ini";

    bool enabled = false;

    // Saturation as it ends up on screen, after the image space's own tint has washed the colours out.
    float saturation = 0.0f;
    float brightness = 0.0f;
    float contrast = 0.0f;

    float eyeAdaptSpeed = 0.0f;
    float eyeAdaptStrength = 0.0f;

    FormList excluded;

    bool Load() {
        CSimpleIniA ini;
        if (!IniTools::Open(ini, FILE)) {
            return false;
        }
        enabled = ini.GetBoolValue("General", "Enabled", true);

        saturation = IniTools::Value(ini, "ImageSpace", "Saturation", 3.0f);
        brightness = IniTools::Value(ini, "ImageSpace", "Brightness", 3.0f);
        contrast = IniTools::Value(ini, "ImageSpace", "Contrast", 3.0f);
        eyeAdaptSpeed = IniTools::Value(ini, "ImageSpace", "EyeAdaptSpeed", 1000.0f);
        eyeAdaptStrength = IniTools::Value(ini, "ImageSpace", "EyeAdaptStrength", 100.0f);

        excluded = IniTools::Forms(ini, "Exclude");
        return true;
    }

    bool Save() const {
        CSimpleIniA ini;
        IniTools::Open(ini, FILE);
        ini.SetBoolValue("General", "Enabled", enabled, nullptr, true);
        IniTools::Set(ini, "ImageSpace", "Saturation", saturation);
        IniTools::Set(ini, "ImageSpace", "Brightness", brightness);
        IniTools::Set(ini, "ImageSpace", "Contrast", contrast);
        IniTools::Set(ini, "ImageSpace", "EyeAdaptSpeed", eyeAdaptSpeed);
        IniTools::Set(ini, "ImageSpace", "EyeAdaptStrength", eyeAdaptStrength);
        return ini.SaveFile(FILE) >= 0;
    }
};

// Skyrok_Rain.ini: multipliers applied to the rain particles of every rainy weather.
struct RainSettings {
    static constexpr auto FILE = "Data/SKSE/Plugins/Skyrok_Rain.ini";

    bool enabled = false;

    float density = 1.0f;  // number of drops
    float speed = 1.0f;    // fall speed
    float width = 1.0f;
    float length = 1.0f;
    float area = 1.0f;     // size of the box around the player in which drops are spawned

    bool Load() {
        CSimpleIniA ini;
        if (!IniTools::Open(ini, FILE)) {
            return false;
        }
        enabled = ini.GetBoolValue("General", "Enabled", true);
        density = IniTools::Factor(ini, "Rain", "Density", 50.0f);
        speed = IniTools::Factor(ini, "Rain", "Speed", 10.0f);
        width = IniTools::Factor(ini, "Rain", "Width", 10.0f);
        length = IniTools::Factor(ini, "Rain", "Length", 10.0f);
        area = IniTools::Factor(ini, "Rain", "Area", 10.0f);
        return true;
    }

    bool Save() const {
        CSimpleIniA ini;
        IniTools::Open(ini, FILE);
        ini.SetBoolValue("General", "Enabled", enabled, nullptr, true);
        IniTools::Set(ini, "Rain", "Density", density);
        IniTools::Set(ini, "Rain", "Speed", speed);
        IniTools::Set(ini, "Rain", "Width", width);
        IniTools::Set(ini, "Rain", "Length", length);
        IniTools::Set(ini, "Rain", "Area", area);
        return ini.SaveFile(FILE) >= 0;
    }
};

// Skyrok_Snow.ini: multipliers applied to the snow particles of every snowy weather.
struct SnowSettings {
    static constexpr auto FILE = "Data/SKSE/Plugins/Skyrok_Snow.ini";

    bool enabled = false;

    float density = 1.0f;  // number of flakes
    float speed = 1.0f;    // fall and spin speed
    float size = 1.0f;
    float area = 1.0f;     // size of the box around the player in which flakes are spawned

    bool Load() {
        CSimpleIniA ini;
        if (!IniTools::Open(ini, FILE)) {
            return false;
        }
        enabled = ini.GetBoolValue("General", "Enabled", true);
        density = IniTools::Factor(ini, "Snow", "Density", 50.0f);
        speed = IniTools::Factor(ini, "Snow", "Speed", 10.0f);
        size = IniTools::Factor(ini, "Snow", "Size", 10.0f);
        area = IniTools::Factor(ini, "Snow", "Area", 10.0f);
        return true;
    }

    bool Save() const {
        CSimpleIniA ini;
        IniTools::Open(ini, FILE);
        ini.SetBoolValue("General", "Enabled", enabled, nullptr, true);
        IniTools::Set(ini, "Snow", "Density", density);
        IniTools::Set(ini, "Snow", "Speed", speed);
        IniTools::Set(ini, "Snow", "Size", size);
        IniTools::Set(ini, "Snow", "Area", area);
        return ini.SaveFile(FILE) >= 0;
    }
};

// Skyrok_Interiors.ini: multipliers applied to every lighting template and every interior cell.
struct InteriorSettings {
    static constexpr auto FILE = "Data/SKSE/Plugins/Skyrok_Interiors.ini";

    bool enabled = false;

    float ambient = 1.0f;
    float directional = 1.0f;
    float fog = 1.0f;

    // Lights never start fading closer than this (0 = leave the distances alone).
    float lightFadeStartMin = 0.0f;
    float lightFadeEndMin = 0.0f;

    // lighting templates left untouched, together with the cells that use them
    FormList excluded;

    bool Load() {
        CSimpleIniA ini;
        if (!IniTools::Open(ini, FILE)) {
            return false;
        }
        enabled = ini.GetBoolValue("General", "Enabled", true);

        ambient = IniTools::Factor(ini, "Interior", "Ambient");
        directional = IniTools::Factor(ini, "Interior", "Directional");
        fog = IniTools::Factor(ini, "Interior", "Fog");
        lightFadeStartMin = IniTools::Value(ini, "Interior", "LightFadeStartMin", 100000.0f);
        lightFadeEndMin = IniTools::Value(ini, "Interior", "LightFadeEndMin", 100000.0f);

        excluded = IniTools::Forms(ini, "Exclude");
        return true;
    }

    bool Save() const {
        CSimpleIniA ini;
        IniTools::Open(ini, FILE);
        ini.SetBoolValue("General", "Enabled", enabled, nullptr, true);
        IniTools::Set(ini, "Interior", "Ambient", ambient);
        IniTools::Set(ini, "Interior", "Directional", directional);
        IniTools::Set(ini, "Interior", "Fog", fog);
        IniTools::Set(ini, "Interior", "LightFadeStartMin", lightFadeStartMin);
        IniTools::Set(ini, "Interior", "LightFadeEndMin", lightFadeEndMin);
        return ini.SaveFile(FILE) >= 0;
    }
};
