#include "menu_bar.h"

#include "ui_events.h"
#include "music_events.h"
#include "event.h"

#include <tinyvk/core/file_dialog.h>
#include <imgui.h>

void MenuBar::OnMenuBar() {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Music Files...")) {
            auto paths = tvk::FileDialog::OpenFiles({ tvk::Filters::Audio() });
            if (!paths.empty())
                Event::Emit(EnqueueTracksEvent{ paths });
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Esc"))
            Event::Emit(QuitEvent{});
        ImGui::EndMenu();
    }
}
