#include "application.h"

#include "event.h"
#include "job_system.h"
#include "audio/audio_player.h"
#include "audio/music_queue.h"
#include "ui/menu_bar.h"
#include "ui/stats_panel.h"
#include "ui/player_panel.h"
#include "ui/queue_panel.h"
#include "ui/ui_events.h"
#include "ui/music_events.h"

#include <imgui.h>

void SkyDropApp::OnStart() {
    Event::Init();
    JobSystem::Init();
    AudioPlayer::Init();
    MusicQueue::Init();

    SetClearColor(0.1f, 0.1f, 0.12f, 1.0f);

    StatsPanel::Init();
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
    StatsPanel::Shutdown();
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

    Event::Emit(StatsUpdatedEvent{
        FPS(),
        DeltaTime() * 1000.0f,
        ElapsedTime(),
        WindowWidth(),
        WindowHeight()
    });
}

void SkyDropApp::OnMenuBar() {
    MenuBar::OnMenuBar();
}

void SkyDropApp::OnUI() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(0, viewport);

    PlayerPanel::OnUI();
    QueuePanel::OnUI();
    StatsPanel::OnUI();
}
