#include "application.h"

#include <imgui.h>

void SkyDropApp::OnStart() {
    SetClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    TVK_LOG_INFO("Skydrop started");
}

void SkyDropApp::OnUpdate() {
    if (tvk::Input::IsKeyPressed(tvk::Key::Escape)) {
        Quit();
    }
}

void SkyDropApp::OnMenuBar() {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Exit", "Esc")) {
            Quit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Stats", nullptr, &_showStats);
        ImGui::EndMenu();
    }
}

void SkyDropApp::OnUI() {
    // Full-screen dockspace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(0, viewport);

    if (_showStats) {
        ImGui::Begin("Stats", &_showStats);
        ImGui::Text("FPS:        %.1f",  FPS());
        ImGui::Text("Frame time: %.3f ms", DeltaTime() * 1000.0f);
        ImGui::Text("Elapsed:    %.1f s",  ElapsedTime());
        ImGui::Separator();
        ImGui::Text("Window: %ux%u", WindowWidth(), WindowHeight());
        ImGui::End();
    }
}
