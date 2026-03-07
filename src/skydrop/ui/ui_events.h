#pragma once

#include <cstdint>

// Emitted when the application should quit (menu, keyboard, etc.)
struct QuitEvent {};

// Emitted to toggle the Stats panel on/off
struct ToggleStatsEvent {};

// Emitted every frame by SkyDropApp with current timing and window info
struct StatsUpdatedEvent {
    float    fps;
    float    deltaMs;
    float    elapsed;
    uint32_t width;
    uint32_t height;
};
