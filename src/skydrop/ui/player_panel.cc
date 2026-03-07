#include "player_panel.h"

#include "theme.h"
#include "music_events.h"
#include "audio/audio_player.h"
#include "audio/music_queue.h"

#include <tinyvk/tinyvk.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
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

// Retro segmented seek/volume widget.
// Draws via DrawList at `pos`, InvisibleButton for interaction.
// Returns true + updates `value` when user drags/clicks.
static bool RetroBar(const char* id, float& value, ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Theme::DrawSegBar(dl, pos, size, value);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    bool changed = false;
    if (ImGui::IsItemActive()) {
        float mx    = ImGui::GetIO().MousePos.x;
        float newV  = (mx - pos.x) / size.x;
        value  = std::clamp(newV, 0.0f, 1.0f);
        changed = true;
    }
    return changed;
}

// Themed transport button — boxy, amber-on-dark
static bool TransportBtn(const char* label) {
    return ImGui::Button(label, { Theme::TransportBtnW, Theme::TransportBtnH });
}

// Toggle button — lights up when active
static bool ToggleBtn(const char* label, bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        Theme::BtnActive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BtnActiveHot);
        ImGui::PushStyleColor(ImGuiCol_Text,          Theme::TextBright);
    }
    bool clicked = ImGui::Button(label, { Theme::ToggleBtnW, Theme::TransportBtnH });
    if (active) ImGui::PopStyleColor(3);
    return clicked;
}

void PlayerPanel::RebuildArtTexture() {
    tvk::Renderer* renderer = tvk::App::Get()->GetRenderer();
    if (!renderer) return;

    std::vector<uint8_t> artPixels;
    int artW = 0, artH = 0;

    if (AudioPlayer::CopyAlbumArt(artPixels, artW, artH)) {
        _artTexture = tvk::Texture::Create(
            renderer, artPixels.data(),
            static_cast<tvk::u32>(artW), static_cast<tvk::u32>(artH),
            tvk::TextureSpec{ .generateMipmaps = false });
    } else {
        static const uint8_t grey[4] = { 20, 14, 3, 255 };
        _artTexture = tvk::Texture::Create(renderer, grey, 1, 1,
            tvk::TextureSpec{ .width = 1, .height = 1, .generateMipmaps = false });
    }

    if (_artTexture) _artTexture->BindToImGui();
}

// ---- Init / Shutdown ----------------------------------------------------

void PlayerPanel::Init() {
    _idTrackChanged = Event::Register<TrackChangedEvent>([](const TrackChangedEvent& e) {
        _title             = e.title;
        _artist            = e.artist;
        _album             = e.album;
        _dur               = e.durationSeconds;
        _pendingRebuildArt = true;
    });

    _idTick = Event::Register<PlaybackTickEvent>([](const PlaybackTickEvent& e) {
        _pos     = e.posSeconds;
        _playing = e.isPlaying;
        _paused  = e.isPaused;
    });
}

void PlayerPanel::Shutdown() {
    Event::Unregister<TrackChangedEvent>(_idTrackChanged);
    Event::Unregister<PlaybackTickEvent>(_idTick);
    _artTexture.reset();
}

// ---- OnUI ---------------------------------------------------------------

void PlayerPanel::OnUI() {
    // Defer texture rebuild to frame boundary to avoid descriptor-set hazards
    if (_pendingRebuildArt) {
        RebuildArtTexture();
        _pendingRebuildArt = false;
    }

    ImDrawList* dl     = ImGui::GetWindowDrawList();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float padX   = ImGui::GetStyle().WindowPadding.x;

    // -------------------------------------------------------------------------
    // Art
    // -------------------------------------------------------------------------
    float artSize = std::clamp(availW * Theme::ArtMaxFrac,
                               Theme::ArtMinPx, Theme::ArtMaxPx);
    float artOffX = (availW - artSize) * 0.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + artOffX);

    ImVec2 artTL = ImGui::GetCursorScreenPos();
    ImVec2 artBR = { artTL.x + artSize, artTL.y + artSize };

    // Draw art image (or placeholder)
    if (_artTexture && _artTexture->IsValid()) {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(_artTexture->GetImGuiTextureID()),
            { artSize, artSize });
    } else {
        // Placeholder: dark amber patterned box
        dl->AddRectFilled(artTL, artBR, Theme::U32ArtBorder);
        dl->AddRectFilled(
            { artTL.x + 2, artTL.y + 2 }, { artBR.x - 2, artBR.y - 2 },
            Theme::U32BgChild);
        // Centred music note icon
        ImVec2 noteSize = ImGui::CalcTextSize(ICON_FA_MUSIC);
        dl->AddText(
            { artTL.x + (artSize - noteSize.x) * 0.5f,
              artTL.y + (artSize - noteSize.y) * 0.5f },
            Theme::U32AccentDim, ICON_FA_MUSIC);
        ImGui::Dummy({ artSize, artSize });
    }

    // Scanline overlay + border drawn ON TOP of art
    Theme::DrawScanlines(dl, artTL, artBR);
    // Dim overlay for depth
    dl->AddRectFilled(artTL, artBR, Theme::U32ArtOverlay);
    // Amber border
    dl->AddRect(artTL, artBR, Theme::U32ArtBorder, 0.0f, 0, 1.5f);

    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Metadata
    // -------------------------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextBright);
    if (!_title.empty()) {
        float tw = ImGui::CalcTextSize(_title.c_str()).x;
        if (tw < availW)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - tw) * 0.5f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availW);
        ImGui::TextUnformatted(_title.c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - ImGui::CalcTextSize("No track loaded").x) * 0.5f);
        ImGui::TextUnformatted("No track loaded");
    }
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    if (!_artist.empty()) {
        float tw = ImGui::CalcTextSize(_artist.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - tw) * 0.5f);
        ImGui::TextUnformatted(_artist.c_str());
    }
    if (!_album.empty()) {
        float tw = ImGui::CalcTextSize(_album.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - tw) * 0.5f);
        ImGui::TextUnformatted(_album.c_str());
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Seek bar (retro segmented)
    // -------------------------------------------------------------------------
    float progress = (_dur > 0.0f) ? (_pos / _dur) : 0.0f;
    const float barW = availW;
    const float barH = Theme::SeekBarH;

    ImVec2 seekPos = ImGui::GetCursorScreenPos();
    if (RetroBar("##seek", progress, seekPos, { barW, barH }))
        Event::Emit(SeekEvent{ progress * _dur });

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

    // Time stamps flanking the bar
    std::string tLeft  = FormatTime(_pos);
    std::string tRight = FormatTime(_dur);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::TextUnformatted(tLeft.c_str());
    ImGui::SameLine(availW - ImGui::CalcTextSize(tRight.c_str()).x + padX * 0.5f);
    ImGui::TextUnformatted(tRight.c_str());
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Transport buttons — centred row
    // -------------------------------------------------------------------------
    {
        float spacing   = ImGui::GetStyle().ItemSpacing.x;
        float rowW      = 4.0f * Theme::TransportBtnW + 3.0f * spacing;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - rowW) * 0.5f);

        if (TransportBtn(ICON_FA_BACKWARD_STEP))                     Event::Emit(SkipPrevEvent{});
        ImGui::SameLine();
        const char* playIcon = (_playing && !_paused) ? ICON_FA_PAUSE : ICON_FA_PLAY;
        if (TransportBtn(playIcon))                                   Event::Emit(PauseToggleEvent{});
        ImGui::SameLine();
        if (TransportBtn(ICON_FA_STOP))                               Event::Emit(StopEvent{});
        ImGui::SameLine();
        if (TransportBtn(ICON_FA_FORWARD_STEP))                       Event::Emit(SkipNextEvent{});
    }

    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Toggle row — shuffle + repeat, centred
    // -------------------------------------------------------------------------
    {
        float spacing   = ImGui::GetStyle().ItemSpacing.x;
        float rowW      = 2.0f * Theme::ToggleBtnW + spacing;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - rowW) * 0.5f);

        bool shuffled = MusicQueue::IsShuffled();
        if (ToggleBtn(ICON_FA_SHUFFLE "  Shuf", shuffled))
            Event::Emit(ShuffleToggleEvent{});

        ImGui::SameLine();

        RepeatMode rm = MusicQueue::RepeatMode_();
        const char* repeatLabel = (rm == RepeatMode::All)  ? ICON_FA_REPEAT "  All"
                                : (rm == RepeatMode::One)  ? ICON_FA_REPEAT "  One"
                                :                            ICON_FA_REPEAT "  Off";
        if (ToggleBtn(repeatLabel, rm != RepeatMode::None))
            Event::Emit(RepeatToggleEvent{});
    }

    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Volume (retro segmented)
    // -------------------------------------------------------------------------
    {
        float iconW  = ImGui::CalcTextSize(ICON_FA_VOLUME_HIGH).x + ImGui::GetStyle().ItemSpacing.x;
        float volBarW = availW - iconW;

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentDim);
        ImGui::TextUnformatted(ICON_FA_VOLUME_HIGH);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImVec2 volPos  = ImGui::GetCursorScreenPos();
        float  volOff  = (Theme::VolumeBarH < ImGui::GetTextLineHeight())
                       ? (ImGui::GetTextLineHeight() - Theme::VolumeBarH) * 0.5f
                       : 0.0f;
        volPos.y += volOff;

        if (RetroBar("##vol", _volume, volPos, { volBarW, Theme::VolumeBarH }))
            Event::Emit(VolumeChangeEvent{ _volume });
    }

    ImGui::Spacing();
}

