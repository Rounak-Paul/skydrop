#include "queue_panel.h"

#include "music_events.h"

#include <tinyvk/assets/icons_font_awesome.h>
#include <imgui.h>

// ---- Statics ------------------------------------------------------------

ListenerID QueuePanel::_idQueueChanged = 0;
std::vector<QueueChangedEvent::Entry> QueuePanel::_tracks;
int32_t QueuePanel::_currentIndex = -1;

// ---- Init / Shutdown ----------------------------------------------------

void QueuePanel::Init() {
    _idQueueChanged = Event::Register<QueueChangedEvent>([](const QueueChangedEvent& e) {
        _tracks       = e.tracks;
        _currentIndex = e.currentIndex;
    });
}

void QueuePanel::Shutdown() {
    Event::Unregister<QueueChangedEvent>(_idQueueChanged);
}

// ---- OnUI ---------------------------------------------------------------

void QueuePanel::OnUI() {
    ImGui::Begin("Queue");

    // --- Toolbar ---
    if (ImGui::Button(ICON_FA_TRASH " Clear")) Event::Emit(QueueClearEvent{});

    ImGui::Separator();

    if (_tracks.empty()) {
        ImGui::TextDisabled(ICON_FA_MUSIC "  Queue is empty — open files via File menu.");
        ImGui::End();
        return;
    }

    ImGui::Text("%zu track(s)", _tracks.size());
    ImGui::Separator();

    // --- Track list ---
    const ImVec2 listSize = ImVec2(0.0f, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild("##tracklist", listSize, false)) {
        int32_t removeIdx = -1;

        for (int32_t i = 0; i < static_cast<int32_t>(_tracks.size()); ++i) {
            const auto& t = _tracks[i];
            bool isCurrent = (i == _currentIndex);

            // Highlight current track
            if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));

            std::string label;
            if (!t.title.empty() && !t.artist.empty())
                label = t.artist + " - " + t.title;
            else if (!t.title.empty())
                label = t.title;
            else
                label = t.path;

            // Duration suffix
            if (t.durationSeconds > 0.0f) {
                int m = static_cast<int>(t.durationSeconds) / 60;
                int s = static_cast<int>(t.durationSeconds) % 60;
                char dur[16];
                std::snprintf(dur, sizeof(dur), "  [%d:%02d]", m, s);
                label += dur;
            }

            if (isCurrent) ImGui::PopStyleColor();

            // Selectable row
            char selId[32];
            std::snprintf(selId, sizeof(selId), "##sel%d", i);
            if (ImGui::Selectable(selId, isCurrent, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 0))) {
                Event::Emit(PlayTrackEvent{ i });
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label.c_str());

            // Right-click context menu
            if (ImGui::BeginPopupContextItem(selId)) {
                if (ImGui::MenuItem(ICON_FA_PLAY  "  Play"))   Event::Emit(PlayTrackEvent{ i });
                if (ImGui::MenuItem(ICON_FA_TRASH " Remove")) removeIdx = i;
                ImGui::EndPopup();
            }
        }

        if (removeIdx >= 0) Event::Emit(RemoveTrackEvent{ removeIdx });
    }
    ImGui::EndChild();

    ImGui::End();
}
