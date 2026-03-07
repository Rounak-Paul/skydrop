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
ListenerID PlayerPanel::_idQueueChanged = 0;

float       PlayerPanel::_pos     = 0.0f;
float       PlayerPanel::_dur     = 0.0f;
bool        PlayerPanel::_playing = false;
bool        PlayerPanel::_paused  = false;
std::string PlayerPanel::_title;
std::string PlayerPanel::_artist;
std::string PlayerPanel::_album;
float       PlayerPanel::_volume  = 1.0f;

int32_t PlayerPanel::_queueIndex = -1;
int32_t PlayerPanel::_queueSize  = 0;

tvk::Ref<tvk::Texture> PlayerPanel::_artTexture;
bool                   PlayerPanel::_pendingRebuildArt = true;
int                    PlayerPanel::_artTexW = 1;
int                    PlayerPanel::_artTexH = 1;

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

// Toggle button — lights up when active; icon-only size
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
        _artTexW = artW;
        _artTexH = artH;
        _artTexture = tvk::Texture::Create(
            renderer, artPixels.data(),
            static_cast<tvk::u32>(artW), static_cast<tvk::u32>(artH),
            tvk::TextureSpec{ .generateMipmaps = false });
    } else {
        _artTexW = 1;
        _artTexH = 1;
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

    _idQueueChanged = Event::Register<QueueChangedEvent>([](const QueueChangedEvent& e) {
        _queueIndex = e.currentIndex;
        _queueSize  = static_cast<int32_t>(e.tracks.size());
    });
}

void PlayerPanel::Shutdown() {
    Event::Unregister<TrackChangedEvent>(_idTrackChanged);
    Event::Unregister<PlaybackTickEvent>(_idTick);
    Event::Unregister<QueueChangedEvent>(_idQueueChanged);
    _artTexture.reset();
}

// ---- OnUI ---------------------------------------------------------------

void PlayerPanel::OnUI() {
    // Defer texture rebuild to frame boundary to avoid descriptor-set hazards
    if (_pendingRebuildArt) {
        RebuildArtTexture();
        _pendingRebuildArt = false;
    }

    ImDrawList* dl      = ImGui::GetWindowDrawList();
    const float fullW   = ImGui::GetContentRegionAvail().x;
    const float availH  = ImGui::GetContentRegionAvail().y;
    const float lineH   = ImGui::GetTextLineHeight();
    const float pad     = Theme::WindowPadX;
    const float innerW  = fullW - pad * 2.0f; // width for padded controls

    // -------------------------------------------------------------------------
    // Art block — fills full width, height proportional
    // -------------------------------------------------------------------------
    const float artH = std::min(fullW * 0.72f, availH * 0.50f);
    const float artW = fullW;

    ImVec2 artTL = ImGui::GetCursorScreenPos();
    ImVec2 artBR = { artTL.x + artW, artTL.y + artH };

    // Art image or placeholder (all via DrawList — no cursor advance yet)
    if (_artTexture && _artTexture->IsValid() && (_artTexW > 1 || _artTexH > 1)) {
        // Scale-to-fill (cover): maintain aspect ratio, center-crop
        float scaleX = artW / static_cast<float>(_artTexW);
        float scaleY = artH / static_cast<float>(_artTexH);
        float scale  = std::max(scaleX, scaleY);
        float scaledW = _artTexW * scale;
        float scaledH = _artTexH * scale;
        float uvOffX  = (scaledW - artW) / (2.0f * scaledW);
        float uvOffY  = (scaledH - artH) / (2.0f * scaledH);
        ImVec2 uv0 = { uvOffX,         uvOffY         };
        ImVec2 uv1 = { 1.0f - uvOffX,  1.0f - uvOffY  };
        dl->AddImage(
            reinterpret_cast<ImTextureID>(_artTexture->GetImGuiTextureID()),
            artTL, artBR, uv0, uv1);
    } else {
        dl->AddRectFilled(artTL, artBR, Theme::U32BgChild);
        ImVec2 noteSize = ImGui::CalcTextSize(ICON_FA_MUSIC);
        dl->AddText(nullptr, 0.0f,
            { artTL.x + (artW - noteSize.x) * 0.5f,
              artTL.y + (artH - noteSize.y) * 0.5f },
            Theme::U32AccentDim, ICON_FA_MUSIC);
    }

    // Retro CRT filter — scanlines + amber tint + vignette
    Theme::DrawRetroFilter(dl, artTL, artBR);

    // Dark gradient rising from the bottom — makes text legible
    const float gradH  = artH * 0.60f;
    ImVec2      gradTL = { artTL.x, artBR.y - gradH };
    dl->AddRectFilledMultiColor(
        gradTL, artBR,
        IM_COL32(5, 3, 1,   0), IM_COL32(5, 3, 1,   0),  // top: transparent
        IM_COL32(5, 3, 1, 240), IM_COL32(5, 3, 1, 240)); // bottom: near-opaque

    // Amber border
    dl->AddRect(artTL, artBR, Theme::U32ArtBorder, 0.0f, 0, 1.0f);

    // ---- Text overlay at bottom of art ------------------------------------
    const float textPad  = pad;
    const float textMaxW = artW - textPad * 2.0f;
    const bool  hasTitle  = !_title.empty();
    const bool  hasArtist = !_artist.empty();

    // Bottom-anchored: figure out total block height
    int lines   = 1 + (hasArtist ? 1 : 0);
    float blockH = lines * lineH + (lines - 1) * 2.0f;
    float textY  = artBR.y - blockH - textPad;

    {
        const char* titleStr = hasTitle ? _title.c_str() : "No track loaded";
        dl->AddText(nullptr, 0.0f,
            { artTL.x + textPad, textY },
            Theme::U32TextPrimary,
            titleStr, nullptr, textMaxW);
        textY += lineH + 2.0f;
    }
    if (hasArtist) {
        dl->AddText(nullptr, 0.0f,
            { artTL.x + textPad, textY },
            Theme::U32TextDim,
            _artist.c_str(), nullptr, textMaxW);
    }

    // Advance cursor past the art block
    ImGui::Dummy({ artW, artH });

    // =========================================================================
    // Controls — use padded inner region
    // =========================================================================
    ImGui::SetCursorPosX(pad);
    ImGui::Spacing();

    // ---- Seek bar -----------------------------------------------------------
    float progress = (_dur > 0.0f) ? (_pos / _dur) : 0.0f;

    ImGui::SetCursorPosX(pad);
    ImVec2 seekPos = ImGui::GetCursorScreenPos();
    if (RetroBar("##seek", progress, seekPos, { innerW, Theme::SeekBarH }))
        Event::Emit(SeekEvent{ progress * _dur });

    // Time stamps
    std::string tLeft  = FormatTime(_pos);
    std::string tRight = FormatTime(_dur);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::SetCursorPosX(pad);
    ImGui::TextUnformatted(tLeft.c_str());
    ImGui::SameLine(pad + innerW - ImGui::CalcTextSize(tRight.c_str()).x);
    ImGui::TextUnformatted(tRight.c_str());
    ImGui::PopStyleColor();

    // ---- Transport buttons --------------------------------------------------
    ImGui::SetWindowFontScale(1.15f);
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float rowW    = 3.0f * Theme::TransportBtnW + 2.0f * spacing;
        ImGui::SetCursorPosX(pad + (innerW - rowW) * 0.5f);

        if (TransportBtn(ICON_FA_BACKWARD_STEP)) Event::Emit(SkipPrevEvent{});
        ImGui::SameLine();
        const char* playIcon = (_playing && !_paused) ? ICON_FA_PAUSE : ICON_FA_PLAY;
        if (TransportBtn(playIcon))              Event::Emit(PauseToggleEvent{});
        ImGui::SameLine();
        if (TransportBtn(ICON_FA_FORWARD_STEP))  Event::Emit(SkipNextEvent{});
    }

    // ---- Toggle row (shuffle + repeat) --------------------------------------
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float rowW    = 2.0f * Theme::ToggleBtnW + spacing;
        ImGui::SetCursorPosX(pad + (innerW - rowW) * 0.5f);

        bool shuffled = MusicQueue::IsShuffled();
        if (ToggleBtn(ICON_FA_SHUFFLE, shuffled))
            Event::Emit(ShuffleToggleEvent{});
        ImGui::SameLine();

        RepeatMode rm = MusicQueue::RepeatMode_();
        // None → ban, All → rotate, One → rotate-right
        const char* repeatIcon = (rm == RepeatMode::All) ? ICON_FA_ROTATE
                                : (rm == RepeatMode::One) ? ICON_FA_ROTATE_RIGHT
                                :                           ICON_FA_BAN;
        if (ToggleBtn(repeatIcon, rm != RepeatMode::None))
            Event::Emit(RepeatToggleEvent{});
    }
    ImGui::SetWindowFontScale(1.0f);

    // ---- Volume bar ---------------------------------------------------------
    {
        ImGui::SetCursorPosX(pad);
        float iconW   = ImGui::CalcTextSize(ICON_FA_VOLUME_HIGH).x
                      + ImGui::GetStyle().ItemSpacing.x;
        float volBarW = innerW - iconW;

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentDim);
        ImGui::TextUnformatted(ICON_FA_VOLUME_HIGH);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImVec2 volPos = ImGui::GetCursorScreenPos();
        volPos.y += (lineH - Theme::VolumeBarH) * 0.5f;
        if (RetroBar("##vol", _volume, volPos, { volBarW, Theme::VolumeBarH }))
            Event::Emit(VolumeChangeEvent{ _volume });
    }

    // ---- Status bar ---------------------------------------------------------
    {
        ImGui::Spacing();
        if (_queueSize > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d/%d",
                _queueIndex >= 0 ? _queueIndex + 1 : 0, _queueSize);
            float tw = ImGui::CalcTextSize(buf).x;
            ImGui::SetCursorPosX(pad + innerW - tw);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();
}

