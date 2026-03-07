#include "player_panel.h"

#include "theme.h"
#include "music_events.h"
#include "audio/audio_player.h"
#include "audio/music_queue.h"
#include "annotations.h"

#include <tinyvk/tinyvk.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ---- Statics ------------------------------------------------------------

ListenerID PlayerPanel::_idTrackChanged = 0;
ListenerID PlayerPanel::_idTick         = 0;
ListenerID PlayerPanel::_idQueueChanged = 0;
ListenerID PlayerPanel::_idWaveform     = 0;

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

std::vector<float> PlayerPanel::_waveform;
std::string        PlayerPanel::_currentTrackPath;

bool  PlayerPanel::_showAnnotPopup = false;
float PlayerPanel::_annotPopupPos  = 0.0f;
char  PlayerPanel::_annotInputBuf[256] = {};

// ---- Helpers ------------------------------------------------------------

static std::string FormatTime(float s) {
    int m = static_cast<int>(s) / 60;
    int sc = static_cast<int>(s) % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, sc);
    return buf;
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

// ---- Waveform seek widget ------------------------------------------------
// Draws the waveform bars, playhead, annotation pins and handles interactions.
// Returns true when the user seeked (newProgress updated).
// Sets *rightClickPos to the time in seconds if the user right-clicked.
static bool DrawWaveformSeek(
    const char*                  id,
    float&                       progress,
    const std::vector<float>&    waveform,
    const std::vector<Annotation>& annots,
    float                        duration,
    ImVec2                       pos,
    ImVec2                       size,
    float*                       rightClickPos)   // nullptr = no right-click reporting
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(pos, { pos.x + size.x, pos.y + size.y }, Theme::U32BgChild);
    dl->AddRect(pos, { pos.x + size.x, pos.y + size.y }, Theme::U32Border, 0.0f, 0, 1.0f);

    const float midY = pos.y + size.y * 0.5f;

    if (!waveform.empty()) {
        const float barW = size.x / static_cast<float>(waveform.size());
        for (size_t i = 0; i < waveform.size(); ++i) {
            const float x = pos.x + static_cast<float>(i) * barW;
            const float h = waveform[i] * (size.y * 0.46f);
            const float binFrac = static_cast<float>(i) / static_cast<float>(waveform.size());
            const ImU32 col = (binFrac <= progress) ? Theme::U32Accent : Theme::U32AccentDim;
            const float bw  = std::max(1.0f, barW - 0.5f);
            dl->AddRectFilled({ x, midY - h }, { x + bw, midY + h }, col);
        }
    } else {
        // Fallback: plain segment bar while waveform is still loading
        Theme::DrawSegBar(dl, pos, size, progress);
    }

    // Playhead vertical line
    const float playX = pos.x + progress * size.x;
    dl->AddLine({ playX, pos.y }, { playX, pos.y + size.y },
                IM_COL32(255, 240, 150, 240), 1.5f);

    // Annotation pins — upward triangle at bottom + dim vertical line
    if (duration > 0.0f) {
        for (const auto& ann : annots) {
            const float ax = pos.x + (ann.posSeconds / duration) * size.x;
            dl->AddLine({ ax, pos.y + 4.0f }, { ax, pos.y + size.y },
                        IM_COL32(255, 155, 40, 150), 1.0f);
            // Triangle pointing up from bottom edge
            dl->AddTriangleFilled(
                { ax,        pos.y + size.y        },
                { ax - 4.5f, pos.y + size.y - 7.0f },
                { ax + 4.5f, pos.y + size.y - 7.0f },
                IM_COL32(255, 155, 40, 220));
        }
    }

    // Invisible button for mouse interaction
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    bool seeked = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float mx = ImGui::GetIO().MousePos.x;
        progress = std::clamp((mx - pos.x) / size.x, 0.0f, 1.0f);
        seeked = true;
    }

    if (rightClickPos && ImGui::IsItemClicked(ImGuiMouseButton_Right) && duration > 0.0f) {
        const float mx = ImGui::GetIO().MousePos.x;
        *rightClickPos = std::clamp((mx - pos.x) / size.x, 0.0f, 1.0f) * duration;
    } else if (rightClickPos) {
        *rightClickPos = -1.0f;  // sentinel: no right-click this frame
    }

    // Hover tooltip — annotation label or time position
    if (ImGui::IsItemHovered() && duration > 0.0f) {
        const float mx = ImGui::GetIO().MousePos.x;
        bool showedAnnot = false;
        for (const auto& ann : annots) {
            const float pixDist = std::fabs((ann.posSeconds / duration) * size.x
                                            - (mx - pos.x));
            if (pixDist < 9.0f) {
                ImGui::SetTooltip("%s\n@ %s", ann.label.c_str(),
                                  FormatTime(ann.posSeconds).c_str());
                showedAnnot = true;
                break;
            }
        }
        if (!showedAnnot) {
            const float hoverSec = std::clamp((mx - pos.x) / size.x, 0.0f, 1.0f) * duration;
            ImGui::SetTooltip("%s  (right-click to annotate)", FormatTime(hoverSec).c_str());
        }
    }

    return seeked;
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
        _waveform.clear();  // old waveform no longer valid
    });

    _idTick = Event::Register<PlaybackTickEvent>([](const PlaybackTickEvent& e) {
        _pos     = e.posSeconds;
        _playing = e.isPlaying;
        _paused  = e.isPaused;
    });

    _idQueueChanged = Event::Register<QueueChangedEvent>([](const QueueChangedEvent& e) {
        _queueIndex = e.currentIndex;
        _queueSize  = static_cast<int32_t>(e.tracks.size());
        // Keep track of current file path for annotation lookup
        if (e.currentIndex >= 0 &&
            e.currentIndex < static_cast<int32_t>(e.tracks.size()))
            _currentTrackPath = e.tracks[e.currentIndex].path;
        else
            _currentTrackPath.clear();
    });

    _idWaveform = Event::Register<WaveformReadyEvent>([](const WaveformReadyEvent&) {
        // Pull the freshly computed waveform from AudioPlayer
        AudioPlayer::CopyWaveform(_waveform);
    });
}

void PlayerPanel::Shutdown() {
    Event::Unregister<TrackChangedEvent>(_idTrackChanged);
    Event::Unregister<PlaybackTickEvent>(_idTick);
    Event::Unregister<QueueChangedEvent>(_idQueueChanged);
    Event::Unregister<WaveformReadyEvent>(_idWaveform);
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
    const float sp      = ImGui::GetStyle().ItemSpacing.y;

    // -------------------------------------------------------------------------
    // Art block — takes all space that controls don't need
    // -------------------------------------------------------------------------
    // Measure the fixed-height controls section so the art can fill the rest.
    const float controlsH =
          Theme::WaveformH                  // waveform bar
        + sp + lineH                        // timestamps row
        + sp + Theme::TransportBtnH         // transport buttons
        + sp + Theme::TransportBtnH         // shuffle/repeat/spatial toggles
        + sp + lineH                        // volume bar
        + sp + lineH;                       // status bar

    const float artH = std::max(80.0f, availH - controlsH - sp);
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

    // ---- Waveform seek bar --------------------------------------------------
    const std::vector<Annotation> annots =
        _currentTrackPath.empty() ? std::vector<Annotation>{}
                                  : Annotations::GetForTrack(_currentTrackPath);

    float progress = (_dur > 0.0f) ? (_pos / _dur) : 0.0f;

    ImGui::SetCursorPosX(pad);
    const ImVec2 wavePos = ImGui::GetCursorScreenPos();
    float rightClickSec = -1.0f;

    if (DrawWaveformSeek("##seek", progress, _waveform, annots,
                         _dur, wavePos, { innerW, Theme::WaveformH },
                         &rightClickSec))
        Event::Emit(SeekEvent{ progress * _dur });

    // Open annotation popup on right-click — must come before any other widget
    if (rightClickSec >= 0.0f) {
        _annotPopupPos = rightClickSec;
        _annotInputBuf[0] = '\0';
        ImGui::OpenPopup("##add_annot");
    }
    // Advance cursor past the waveform (InvisibleButton already consumed it)
    ImGui::SetCursorScreenPos({ wavePos.x, wavePos.y + Theme::WaveformH });

    // Annotation add popup
    if (ImGui::BeginPopup("##add_annot")) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextPrimary);
        ImGui::TextUnformatted("Add annotation");
        ImGui::PopStyleColor();
        ImGui::Separator();

        char timeBuf[16];
        std::snprintf(timeBuf, sizeof(timeBuf), "@ %s", FormatTime(_annotPopupPos).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        ImGui::TextUnformatted(timeBuf);
        ImGui::PopStyleColor();

        ImGui::SetNextItemWidth(200.0f);
        const bool enter = ImGui::InputText("##annot_label", _annotInputBuf,
                                            sizeof(_annotInputBuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("OK") || enter) && _annotInputBuf[0] != '\0') {
            if (!_currentTrackPath.empty())
                Annotations::Add(_currentTrackPath, _annotPopupPos, _annotInputBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Time stamps — inline, no spacing above
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 1.0f });
    const std::string tLeft  = FormatTime(_pos);
    const std::string tRight = FormatTime(_dur);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::SetCursorPosX(pad);
    ImGui::TextUnformatted(tLeft.c_str());
    ImGui::SameLine(pad + innerW - ImGui::CalcTextSize(tRight.c_str()).x);
    ImGui::TextUnformatted(tRight.c_str());
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

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

    // ---- Toggle row (shuffle + repeat) ------------------------------------
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float rowW    = 2.0f * Theme::ToggleBtnW + 1.0f * spacing;
        ImGui::SetCursorPosX(pad + (innerW - rowW) * 0.5f);

        bool shuffled = MusicQueue::IsShuffled();
        if (ToggleBtn(ICON_FA_SHUFFLE, shuffled))
            Event::Emit(ShuffleToggleEvent{});
        ImGui::SameLine();

        RepeatMode rm = MusicQueue::RepeatMode_();
        const char* repeatIcon = (rm == RepeatMode::All)  ? ICON_FA_ROTATE
                                : (rm == RepeatMode::One) ? ICON_FA_ROTATE_RIGHT
                                :                           ICON_FA_BAN;
        if (ToggleBtn(repeatIcon, rm != RepeatMode::None))
            Event::Emit(RepeatToggleEvent{});
    }
    ImGui::SetWindowFontScale(1.0f);

    // ---- Volume bar ---------------------------------------------------------
    {
        ImGui::SetCursorPosX(pad);
        const float iconW   = ImGui::CalcTextSize(ICON_FA_VOLUME_HIGH).x
                            + ImGui::GetStyle().ItemSpacing.x;
        const float volBarW = innerW - iconW;

        const float lineH2 = ImGui::GetTextLineHeight();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentDim);
        ImGui::TextUnformatted(ICON_FA_VOLUME_HIGH);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImVec2 volPos = ImGui::GetCursorScreenPos();
        volPos.y += (lineH2 - Theme::VolumeBarH) * 0.5f;
        ImDrawList* dl2 = ImGui::GetWindowDrawList();
        Theme::DrawSegBar(dl2, volPos, { volBarW, Theme::VolumeBarH }, _volume);
        ImGui::SetCursorScreenPos(volPos);
        ImGui::InvisibleButton("##vol", { volBarW, lineH2 });
        if (ImGui::IsItemActive()) {
            const float mx = ImGui::GetIO().MousePos.x;
            _volume = std::clamp((mx - volPos.x) / volBarW, 0.0f, 1.0f);
            Event::Emit(VolumeChangeEvent{ _volume });
        }
    }

    // ---- Status bar — flush to bottom of available space --------------------
    if (_queueSize > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d/%d",
            _queueIndex >= 0 ? _queueIndex + 1 : 0, _queueSize);
        const float tw = ImGui::CalcTextSize(buf).x;
        ImGui::SetCursorPosX(pad + innerW - tw);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        ImGui::TextUnformatted(buf);
        ImGui::PopStyleColor();
    }
}

