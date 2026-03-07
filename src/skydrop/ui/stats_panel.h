#pragma once

#include "event.h"

class StatsPanel {
public:
    static void Init();
    static void Shutdown();
    static void OnUI();

    static bool IsVisible() { return _show; }

private:
    static bool       _show;
    static ListenerID _toggleID;
    static ListenerID _statsID;

    static float    _fps;
    static float    _deltaMs;
    static float    _elapsed;
    static uint32_t _width;
    static uint32_t _height;
};
