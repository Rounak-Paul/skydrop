#include "player_panel.h"

#include "music_events.h"
#include "audio/audio_player.h"
#include "audio/music_queue.h"

#include <tinyvk/tinyvk.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <imgui.h>

#include <cstdio>
#include <vector>

// ---- Statics ------------------------------------------------------------

ListenerID PlayerPanel::_idTrackChanged = 0;
ListenerID PlayerPanel::_idTick         = 0;

float       PlayerPanel::_pos     = 0.0f;
float       PlayerPanel::_dur     = 0.0f;
bool        PlayerPanel::_playing = false;
bool        PlayerPanel::_paused  = false;
std::string PlayerPanel::_title;
std::string PlayerPanel::_artist;
std::string PlayerPanel::_album;
float       PlayerPanel::_volume  = 1.0f;

tvk::Ref<tvk::Texture> PlayerPanel::_artTexture;
bool                   PlayerPanel::_pendingRebuildArt = true;

// ---- Helpers ------------------------------------------------------------

static std::string FormatTime(float s) {
    int m = static_cast<int>(s) / 60;
    int sc = static_cast<int>(s) % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, sc);
    return buf;
}

void PlayerPanel::RebuildArtTexture() {
    tvk::Renderer* renderer = tvk::App::Get()->GetRenderer();
    if (!renderer) return;

    std::vector<uint8_t> artPixels;
    int artW = 0, artH = 0;

    if (AudioPlayer::CopyAlbumArt(artPixels, artW, artH)) {
        _artTexture = tvk::Texture::Create(
            renderer,
            artPixels.data(),
            static_cast<tvk::u32>(artW),
            static_cast<tvk::u32>(artH),
            tvk::TextureSpec{ .generateMipmaps = false });
    } else {
        // 1×1 grey placeholder
        static const uint8_t grey[4] = { 80, 80, 80, 255 };
        _artTexture = tvk::Texture::Create(renderer, grey, 1, 1,
            tvk::TextureSpec{ .width = 1, .height = 1, .generateMipmaps = false });
    }

    if (_artTexture) _artTexture->BindToImGui();
}

// ---- Init / Shutdown ----------------------------------------------------

void PlayerPanel::Init() {
    _idTrackChanged = Event::Register<TrackChangedEvent>([](const TrackChangedEvent& e) {
        _title               = e.title;
        _artist              = e.artist;
        _album               = e.album;
        _dur                 = e.durationSeconds;
        _pendingRebuildArt   = true;  // deferred — rebuild at start of next frame
    });

    _idTick = Event::Register<PlaybackTickEvent>([](const PlaybackTickEvent& e) {
        _pos     = e.posSeconds;
        _playing = e.isPlaying;
        _paused  = e.isPaused;
    });

    // Placeholder texture is built on first OnUI() via _pendingRebuildArt = true
}

void PlayerPanel::Shutdown() {
    Event::Unregister<TrackChangedEvent>(_idTrackChanged);
    Event::Unregister<PlaybackTickEvent>(_idTick);
    _artTexture.reset();
}

// ---- OnUI ---------------------------------------------------------------

void PlayerPanel::OnUI() {
    // Rebuild art texture here — never mid-frame — so the old descriptor set
    // lives through any in-flight draw calls from the previous frame.
    if (_pendingRebuildArt) {
        RebuildArtTexture();
        _pendingRebuildArt = false;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("Player", nullptr, flags);

    const float panelW  = ImGui::GetContentRegionAvail().x;
    const float artSize = 128.0f;

    // --- Album art ---
    if (_artTexture && _artTexture->IsValid()) {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(_artTexture->GetImGuiTextureID()),
            ImVec2(artSize, artSize));
    } else {
        ImGui::Dummy(ImVec2(artSize, artSize));
    }

    ImGui::SameLine();

    // --- Metadata ---
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panelW - artSize - 16.0f);
    if (!_title.empty())  ImGui::TextUnformatted(_title.c_str());
    else                  ImGui::TextDisabled("No track loaded");
    if (!_artist.empty()) { ImGui::Spacing(); ImGui::TextDisabled("%s", _artist.c_str()); }
    if (!_album.empty())  {                   ImGui::TextDisabled("%s", _album.c_str()); }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Progress bar / seek ---
    float progress = (_dur > 0.0f) ? (_pos / _dur) : 0.0f;
    std::string timeStr = FormatTime(_pos) + " / " + FormatTime(_dur);

    ImGui::PushItemWidth(-1.0f);
    if (ImGui::SliderFloat("##seek", &progress, 0.0f, 1.0f, "")) {
        Event::Emit(SeekEvent{ progress * _dur });
    }
    ImGui::PopItemWidth();
    ImGui::TextDisabled("%s", timeStr.c_str());

    ImGui::Spacing();

    // --- Transport controls ---
    float btnW = 40.0f;
    float totalBtnW = btnW * 5 + ImGui::GetStyle().ItemSpacing.x * 4;
    ImGui::SetCursorPosX((panelW - totalBtnW) * 0.5f);

    if (ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(btnW, 0))) Event::Emit(SkipPrevEvent{});
    ImGui::SameLine();
    const char* playLabel = (_playing && !_paused) ? ICON_FA_PAUSE : ICON_FA_PLAY;
    if (ImGui::Button(playLabel, ImVec2(btnW, 0))) Event::Emit(PauseToggleEvent{});
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP, ImVec2(btnW, 0)))         Event::Emit(StopEvent{});
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(btnW, 0))) Event::Emit(SkipNextEvent{});
    ImGui::SameLine();

    // Shuffle toggle
    bool shuffled = MusicQueue::IsShuffled();
    if (shuffled) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(ICON_FA_SHUFFLE, ImVec2(btnW, 0)))      Event::Emit(ShuffleToggleEvent{});
    if (shuffled) ImGui::PopStyleColor();

    ImGui::Spacing();

    // --- Repeat mode ---
    RepeatMode rm = MusicQueue::RepeatMode_();
    const char* repeatSuffix = (rm == RepeatMode::None) ? " Off"
                             : (rm == RepeatMode::All)  ? " All"
                                                        : " One";
    char repeatLabel[32];
    std::snprintf(repeatLabel, sizeof(repeatLabel), "%s%s", ICON_FA_REPEAT, repeatSuffix);
    if (rm != RepeatMode::None)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(repeatLabel)) Event::Emit(RepeatToggleEvent{});
    if (rm != RepeatMode::None) ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Volume ---
    ImGui::SetNextItemWidth(panelW * 0.6f);
    if (ImGui::SliderFloat(ICON_FA_VOLUME_HIGH, &_volume, 0.0f, 1.0f)) {
        Event::Emit(VolumeChangeEvent{ _volume });
    }

    ImGui::End();
}
