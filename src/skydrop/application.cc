#include "application.h"

#include "event.h"
#include "job_system.h"
#include "audio/audio_player.h"
#include "audio/music_queue.h"
#include "ui/menu_bar.h"
#include "ui/player_panel.h"
#include "ui/queue_panel.h"
#include "ui/theme.h"
#include "ui/ui_events.h"
#include "ui/music_events.h"

#include <imgui.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <GLFW/glfw3.h>

void SkyDropApp::OnStart() {
    Event::Init();
    JobSystem::Init();
    AudioPlayer::Init();
    MusicQueue::Init();

    Theme::Apply();
    SetClearColor(0.047f, 0.035f, 0.008f, 1.0f);

    glfwSetWindowSizeLimits(
        static_cast<GLFWwindow*>(GetWindow()->GetNativeHandle()),
        200, 300, 300, 500);

    PlayerPanel::Init();
    QueuePanel::Init();

    _quitID = Event::Register<QuitEvent>([this](const QuitEvent&) { Quit(); });

    _volumeID = Event::Register<VolumeChangeEvent>([](const VolumeChangeEvent& e) {
        AudioPlayer::SetVolume(e.volume);
    });

    _pauseID = Event::Register<PauseToggleEvent>([](const PauseToggleEvent&) {
        if (AudioPlayer::IsPaused()) AudioPlayer::Resume();
        else                         AudioPlayer::Pause();
    });

    _seekID = Event::Register<SeekEvent>([](const SeekEvent& e) {
        AudioPlayer::Seek(e.posSeconds);
    });

    TVK_LOG_INFO("Skydrop started");
}

void SkyDropApp::OnStop() {
    Event::Unregister<QuitEvent>(_quitID);
    Event::Unregister<VolumeChangeEvent>(_volumeID);
    Event::Unregister<PauseToggleEvent>(_pauseID);
    Event::Unregister<SeekEvent>(_seekID);

    QueuePanel::Shutdown();
    PlayerPanel::Shutdown();
    MusicQueue::Shutdown();
    AudioPlayer::Shutdown();
    JobSystem::Shutdown();
    Event::Shutdown();
}

void SkyDropApp::OnUpdate() {
    if (tvk::Input::IsKeyPressed(tvk::Key::Escape))
        Event::Emit(QuitEvent{});

    AudioPlayer::Update();

    Event::Emit(PlaybackTickEvent{
        AudioPlayer::GetPosition(),
        AudioPlayer::GetDuration(),
        AudioPlayer::IsPlaying(),
        AudioPlayer::IsPaused()
    });

}

void SkyDropApp::OnMenuBar() {
    MenuBar::OnMenuBar();
}

void SkyDropApp::OnUI() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menuH = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos({ vp->Pos.x, vp->Pos.y + menuH });
    ImGui::SetNextWindowSize({ vp->Size.x, vp->Size.y - menuH });
    constexpr ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoDecoration
      | ImGuiWindowFlags_NoMove
      | ImGuiWindowFlags_NoBringToFrontOnFocus
      | ImGuiWindowFlags_NoScrollWithMouse
      | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##shell", nullptr, wf);

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(ICON_FA_MUSIC " Player")) {
            PlayerPanel::OnUI();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_LIST " Queue")) {
            QueuePanel::OnUI();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
