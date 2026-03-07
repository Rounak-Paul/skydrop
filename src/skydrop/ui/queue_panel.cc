#include "queue_panel.h"

#include "theme.h"
#include "music_events.h"

#include <tinyvk/assets/icons_font_awesome.h>
#include <imgui.h>

#include <cstdio>
#include <string>

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
    const float availW = ImGui::GetContentRegionAvail().x;

    // ---- Toolbar --------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Button,        Theme::BtnNormal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BtnHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::BtnActive);
    if (ImGui::Button(ICON_FA_TRASH " Clear"))
        Event::Emit(QueueClearEvent{});
    ImGui::PopStyleColor(3);

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::SameLine(availW - ImGui::CalcTextSize("000 tracks").x);
    if (!_tracks.empty()) {
        char cnt[32];
        std::snprintf(cnt, sizeof(cnt), "%zu track%s",
                      _tracks.size(), _tracks.size() == 1 ? "" : "s");
        ImGui::TextUnformatted(cnt);
    }
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Separator, Theme::Border);
    ImGui::Separator();
    ImGui::PopStyleColor();

    if (_tracks.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        float tw = ImGui::CalcTextSize("Open files via  File  menu").x;
        ImGui::SetCursorPosX((availW - tw) * 0.5f);
        ImGui::TextUnformatted(ICON_FA_MUSIC "  Open files via  File  menu");
        ImGui::PopStyleColor();
        return;
    }

    // ---- Track list -----------------------------------------------------
    const ImVec2 listSize = { 0.0f, ImGui::GetContentRegionAvail().y };
    if (!ImGui::BeginChild("##tracklist", listSize, false,
                           ImGuiWindowFlags_NoScrollbar)) {
        ImGui::EndChild();
        return;
    }

    int32_t removeIdx = -1;

    for (int32_t i = 0; i < static_cast<int32_t>(_tracks.size()); ++i) {
        const auto& t          = _tracks[i];
        const bool  isCurrent  = (i == _currentIndex);

        // Build label
        std::string label;
        if (!t.title.empty() && !t.artist.empty())
            label = t.artist + "  \xe2\x80\x93  " + t.title; // en-dash
        else if (!t.title.empty())
            label = t.title;
        else
            label = t.path;

        char selId[32];
        std::snprintf(selId, sizeof(selId), "##row%d", i);

        // Row background highlight for current track
        if (isCurrent) {
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            rowMin.x -= ImGui::GetStyle().WindowPadding.x;
            ImVec2 rowMax = { rowMin.x + ImGui::GetWindowWidth(),
                              rowMin.y + ImGui::GetTextLineHeightWithSpacing() };
            ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax,
                IM_COL32(80, 53, 0, 80));
        }

        // Leading now-playing icon
        if (isCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentHot);
            ImGui::TextUnformatted(ICON_FA_CIRCLE_DOT);
            ImGui::PopStyleColor();
            ImGui::SameLine();
        } else {
            // Indent by icon width for alignment
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX()
                + ImGui::CalcTextSize(ICON_FA_CIRCLE_DOT).x
                + ImGui::GetStyle().ItemSpacing.x);
        }

        // Duration suffix
        if (t.durationSeconds > 0.0f) {
            int m = static_cast<int>(t.durationSeconds) / 60;
            int s = static_cast<int>(t.durationSeconds) % 60;
            char dur[12];
            std::snprintf(dur, sizeof(dur), " [%d:%02d]", m, s);
            label += dur;
        }

        // Coloured selectable
        ImGui::PushStyleColor(ImGuiCol_Text,
            isCurrent ? Theme::AccentHot : Theme::TextPrimary);
        if (ImGui::Selectable(label.c_str(), isCurrent,
                              ImGuiSelectableFlags_SpanAllColumns))
            Event::Emit(PlayTrackEvent{ i });
        ImGui::PopStyleColor();

        // Right-click context
        if (ImGui::BeginPopupContextItem(selId)) {
            if (ImGui::MenuItem(ICON_FA_PLAY  "  Play"))
                Event::Emit(PlayTrackEvent{ i });
            if (ImGui::MenuItem(ICON_FA_TRASH "  Remove"))
                removeIdx = i;
            ImGui::EndPopup();
        }
    }

    if (removeIdx >= 0) Event::Emit(RemoveTrackEvent{ removeIdx });

    ImGui::EndChild();
}

