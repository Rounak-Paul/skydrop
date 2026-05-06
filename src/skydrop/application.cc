#include "application.h"

#include "event.h"
#include "job_system.h"
#include "session.h"
#include "audio/audio_player.h"
#include "audio/music_queue.h"
#include "ui/menu_bar.h"
#include "ui/player_panel.h"
#include "ui/queue_panel.h"
#include "ui/sound_panel.h"
#include "ui/theme.h"
#include "ui/ui_events.h"
#include "ui/music_events.h"

#include <tinyvk/core/window.h>
#include <imgui.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <GLFW/glfw3.h>

// ---- Drag-drop GLFW callback (file OS drop) --------------------------------

static void GlfwDropCallback(GLFWwindow* /*win*/, int count, const char** paths) {
    std::vector<std::string> files;
    files.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        // Accept known audio extensions only
        const std::string p(paths[i]);
        const auto ext = [&]() {
            const size_t dot = p.rfind('.');
            if (dot == std::string::npos) return std::string{};
            std::string e = p.substr(dot + 1);
            for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return e;
        }();
        static constexpr const char* kExts[] = { "mp3","flac","ogg","wav","opus","aac","m4a" };
        for (const char* ok : kExts)
            if (ext == ok) { files.push_back(p); break; }
    }
    if (!files.empty())
        Event::Emit(EnqueueTracksEvent{ std::move(files) });
}

// ---- Helpers ---------------------------------------------------------------

void SkyDropApp::RegisterDropCallback() {
    tvk::Window* win = GetWindow();
    if (!win) return;
    GLFWwindow* glfw = win->GetNativeHandle();
    if (!glfw) return;
    glfwSetDropCallback(glfw, GlfwDropCallback);
}

void SkyDropApp::SaveSession() {
    // Pull current queue from the last QueueChangedEvent cached state
    // (We keep _queuePaths in memory via the listener below.)
    // Build state from MusicQueue and AudioPlayer.
    SessionState s;
    s.volume     = _currentVolume;
    s.posSeconds = _currentPos;
    s.shuffle    = MusicQueue::IsShuffled();
    s.repeatMode = static_cast<int>(MusicQueue::RepeatMode_());
    // Queue paths + index are stored via _queueChangedID listener below
    // (cached in _sessionQueue / _sessionIndex).
    // We use the dedicated members set in the listener.
    s.queuePaths  = _sessionQueuePaths;
    s.currentIndex= _sessionCurrentIndex;
    Session::Save(s);
}

void SkyDropApp::OnStart() {
    Event::Init();
    JobSystem::Init();
    AudioPlayer::Init();
    MusicQueue::Init();

    Theme::Apply();
    SetClearColor(0.047f, 0.035f, 0.008f, 1.0f);

    PlayerPanel::Init();
    QueuePanel::Init();

    // ---- Core event wiring -------------------------------------------------
    _quitID = Event::Register<QuitEvent>([this](const QuitEvent&) { Quit(); });

    _volumeID = Event::Register<VolumeChangeEvent>([this](const VolumeChangeEvent& e) {
        _currentVolume = e.volume;
        AudioPlayer::SetVolume(e.volume);
    });

    _pauseID = Event::Register<PauseToggleEvent>([](const PauseToggleEvent&) {
        if (AudioPlayer::IsPaused()) AudioPlayer::Resume();
        else                         AudioPlayer::Pause();
    });

    _seekID = Event::Register<SeekEvent>([](const SeekEvent& e) {
        AudioPlayer::Seek(e.posSeconds);
    });

    // ---- Cache queue snapshot for session save -----------------------------
    _queueChangedID = Event::Register<QueueChangedEvent>([this](const QueueChangedEvent& e) {
        _sessionCurrentIndex = e.currentIndex;
        _sessionQueuePaths.clear();
        _sessionQueuePaths.reserve(e.tracks.size());
        for (const auto& t : e.tracks)
            _sessionQueuePaths.push_back(t.path);
    });

    // ---- Register OS drag-drop ---------------------------------------------
    RegisterDropCallback();

    // ---- Restore previous session ------------------------------------------
    SessionState saved;
    if (Session::Load(saved)) {
        // Restore volume
        _currentVolume = saved.volume;
        AudioPlayer::SetVolume(saved.volume);

        // Restore shuffle / repeat
        if (saved.shuffle)
            Event::Emit(ShuffleToggleEvent{});
        for (int i = 0; i < saved.repeatMode; ++i)
            Event::Emit(RepeatToggleEvent{});

        // Re-enqueue tracks — this will auto-start the first one
        if (!saved.queuePaths.empty()) {
            Event::Emit(EnqueueTracksEvent{ saved.queuePaths });

            // Jump to saved index (EnqueueTracks starts at 0)
            if (saved.currentIndex > 0)
                Event::Emit(PlayTrackEvent{ saved.currentIndex });

            // Seek to saved position after a brief delay — we defer it
            // via a one-shot listener that fires when the track is loaded
            const float resumePos = saved.posSeconds;
            static ListenerID s_resumeID = 0;
            s_resumeID = Event::Register<TrackChangedEvent>([resumePos](const TrackChangedEvent&) {
                if (resumePos > 1.0f)
                    Event::Emit(SeekEvent{ resumePos });
                Event::Unregister<TrackChangedEvent>(s_resumeID);
                s_resumeID = 0;
            });
        }
    }

    TVK_LOG_INFO("Skydrop started");
}

void SkyDropApp::OnStop() {
    // Persist session before tearing down
    _currentPos = AudioPlayer::GetPosition();
    SaveSession();

    Event::Unregister<QuitEvent>(_quitID);
    Event::Unregister<VolumeChangeEvent>(_volumeID);
    Event::Unregister<PauseToggleEvent>(_pauseID);
    Event::Unregister<SeekEvent>(_seekID);
    Event::Unregister<QueueChangedEvent>(_queueChangedID);

    QueuePanel::Shutdown();
    PlayerPanel::Shutdown();
    MusicQueue::Shutdown();
    AudioPlayer::Shutdown();
    JobSystem::Shutdown();
    Event::Shutdown();
}

void SkyDropApp::OnUpdate() {
    // --- Keyboard shortcuts (only when no text widget is focused) -----------
    if (!ImGui::GetIO().WantTextInput) {
        // Quit
        if (tvk::Input::IsKeyPressed(tvk::Key::Escape))
            Event::Emit(QuitEvent{});

        // Play / Pause
        if (tvk::Input::IsKeyPressed(tvk::Key::Space))
            Event::Emit(PauseToggleEvent{});

        // Skip next / previous
        if (tvk::Input::IsKeyPressed(tvk::Key::N))
            Event::Emit(SkipNextEvent{});
        if (tvk::Input::IsKeyPressed(tvk::Key::P))
            Event::Emit(SkipPrevEvent{});

        // Volume  (Up/Down arrow — 5% steps)
        if (tvk::Input::IsKeyPressed(tvk::Key::Up)) {
            _currentVolume = std::min(1.0f, _currentVolume + 0.05f);
            Event::Emit(VolumeChangeEvent{ _currentVolume });
        }
        if (tvk::Input::IsKeyPressed(tvk::Key::Down)) {
            _currentVolume = std::max(0.0f, _currentVolume - 0.05f);
            Event::Emit(VolumeChangeEvent{ _currentVolume });
        }

        // Open files  (Ctrl/Cmd + O)
        const bool mod = tvk::Input::IsKeyPressed(tvk::Key::LeftControl)
                      || tvk::Input::IsKeyPressed(tvk::Key::RightControl)
                      || tvk::Input::IsKeyPressed(tvk::Key::LeftSuper)
                      || tvk::Input::IsKeyPressed(tvk::Key::RightSuper);
        if (mod && tvk::Input::IsKeyPressed(tvk::Key::O)) {
            auto paths = tvk::FileDialog::OpenFiles({ tvk::Filters::Audio() });
            if (!paths.empty())
                Event::Emit(EnqueueTracksEvent{ paths });
        }
    }

    // --- Cache current position for session save ----------------------------
    _currentPos = AudioPlayer::GetPosition();

    // --- Audio tick ---------------------------------------------------------
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##shell", nullptr, wf);
    ImGui::PopStyleVar();

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(ICON_FA_MUSIC)) {
            PlayerPanel::OnUI();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_LIST)) {
            QueuePanel::OnUI();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_HEADPHONES)) {
            SoundPanel::OnUI();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
