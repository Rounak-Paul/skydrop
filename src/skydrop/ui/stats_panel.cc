#include "stats_panel.h"

#include "ui_events.h"

#include <imgui.h>

bool       StatsPanel::_show     = false;
ListenerID StatsPanel::_toggleID = 0;
ListenerID StatsPanel::_statsID  = 0;
float      StatsPanel::_fps      = 0.0f;
float      StatsPanel::_deltaMs  = 0.0f;
float      StatsPanel::_elapsed  = 0.0f;
uint32_t   StatsPanel::_width    = 0;
uint32_t   StatsPanel::_height   = 0;

void StatsPanel::Init() {
    _toggleID = Event::Register<ToggleStatsEvent>(
        [](const ToggleStatsEvent&) { _show = !_show; });

    _statsID = Event::Register<StatsUpdatedEvent>(
        [](const StatsUpdatedEvent& e) {
            _fps     = e.fps;
            _deltaMs = e.deltaMs;
            _elapsed = e.elapsed;
            _width   = e.width;
            _height  = e.height;
        });
}

void StatsPanel::Shutdown() {
    Event::Unregister<ToggleStatsEvent>(_toggleID);
    Event::Unregister<StatsUpdatedEvent>(_statsID);
}

void StatsPanel::OnUI() {
    if (!_show) return;

    ImGui::Begin("Stats", &_show);
    ImGui::Text("FPS:        %.1f",   _fps);
    ImGui::Text("Frame time: %.3f ms", _deltaMs);
    ImGui::Text("Elapsed:    %.1f s",  _elapsed);
    ImGui::Separator();
    ImGui::Text("Window: %ux%u", _width, _height);
    ImGui::End();
}
