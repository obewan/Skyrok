#pragma once

// What the game is doing right now, published for the ReShade shaders shipped with Skyrok.
// Written on the game's main thread, read on the render thread.
namespace GameState {
    inline std::atomic<float> rain{0.0f};      // 0..1: how much it is raining where the player stands (0 indoors)
    inline std::atomic<float> interior{0.0f};  // 1 inside, 0 outside
    inline std::atomic<float> night{0.0f};     // 0 by day, 1 at night, in between during sunrise and sunset
    // 0..1: how dark the player's surroundings are. Outdoors it is the night level; indoors it
    // comes from the room's own ambient light, so a dim cave counts and a bright hall does not.
    inline std::atomic<float> darkness{0.0f};

    // Refreshes the values above. Call from the main thread.
    void Update();

    // 0 when the weather is not rainy, otherwise how hard it rains: about 0.5 for ordinary rain,
    // 1 for a storm. Judged from the weather's own rain particles, before Skyrok's heavy rain setting.
    float RainIntensity(const RE::TESWeather *weather);
}

namespace ReShadeLink {
    // Registers Skyrok with ReShade if it is loaded and accepts add-ons. Safe to call when it is not.
    bool Connect();
}
