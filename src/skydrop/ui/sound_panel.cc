#include "sound_panel.h"

#include "theme.h"
#include "audio/audio_player.h"

#include <imgui.h>
#include <tinyvk/assets/icons_font_awesome.h>

#include <algorithm>
#include <cmath>

// ---- Statics ----------------------------------------------------------------

int   SoundPanel::_spatialPreset  = 0;
float SoundPanel::_spatialAzimuth = 0.0f;
float SoundPanel::_bassDb         = 0.0f;
float SoundPanel::_midDb          = 0.0f;
float SoundPanel::_trebleDb       = 0.0f;

// ---- Helpers ----------------------------------------------------------------

static inline float DbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Active-highlighted button; returns true on click.
static bool PresetBtn(const char* label, bool active, ImVec2 size) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        Theme::BtnActive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BtnActiveHot);
        ImGui::PushStyleColor(ImGuiCol_Text,          Theme::TextBright);
    }
    const bool clicked = ImGui::Button(label, size);
    if (active) ImGui::PopStyleColor(3);
    return clicked;
}

// Azimuth wheel — drawn with ImDrawList, returns true when azimuth changed.
static bool DrawAzimuthWheel(const char* id, float& azimuthDeg,
                              ImVec2 center, float radius)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddCircleFilled(center, radius, Theme::U32BgChild, 48);
    dl->AddCircle(center, radius, Theme::U32Border, 48, 1.5f);

    constexpr float tickLen = 5.0f;
    for (int i = 0; i < 4; ++i) {
        const float ang = i * (3.14159265f * 0.5f);
        const float sx = std::sin(ang), cx = std::cos(ang);
        dl->AddLine(
            { center.x + sx * (radius - tickLen), center.y - cx * (radius - tickLen) },
            { center.x + sx *  radius,            center.y - cx *  radius            },
            Theme::U32Border, 1.5f);
    }

    const float lh   = ImGui::GetTextLineHeight();
    const float lOff = radius + 11.0f;
    auto addLabel = [&](const char* s, float cx2, float cy2) {
        dl->AddText(nullptr, 0.0f, { cx2 - ImGui::CalcTextSize(s).x * 0.5f, cy2 - lh * 0.5f },
                    Theme::U32TextDim, s);
    };
    addLabel("F", center.x,        center.y - lOff);
    addLabel("B", center.x,        center.y + lOff);
    addLabel("L", center.x - lOff - ImGui::CalcTextSize("L").x * 0.5f, center.y);
    addLabel("R", center.x + lOff - ImGui::CalcTextSize("R").x * 0.5f, center.y);

    const float radAz = azimuthDeg * (3.14159265f / 180.0f);
    const ImVec2 dot  = { center.x + std::sin(radAz) * radius * 0.72f,
                          center.y - std::cos(radAz) * radius * 0.72f };

    dl->AddLine(center, dot, Theme::U32AccentDim, 1.5f);
    dl->AddCircleFilled(dot, 6.5f, Theme::U32Accent, 16);
    dl->AddCircle(dot, 6.5f, Theme::U32AccentHot, 16, 1.5f);
    dl->AddCircleFilled(center, 3.5f, Theme::U32TextDim, 12);

    ImGui::SetCursorScreenPos({ center.x - radius, center.y - radius });
    ImGui::InvisibleButton(id, { radius * 2.0f, radius * 2.0f });

    bool changed = false;
    if (ImGui::IsItemActive()) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const float dx = mp.x - center.x;
        const float dy = mp.y - center.y;
        if (dx * dx + dy * dy > 4.0f) {
            azimuthDeg = std::atan2(dx, -dy) * (180.0f / 3.14159265f);
            changed    = true;
        }
    }
    return changed;
}

// Retro segmented EQ bar — range −12…+12 dB, center = 0 dB.
// Segments fill outward from centre: amber = boost, dim amber = cut.
// Returns true when the value changed.
static bool DrawEQBar(const char* id, const char* label, float& db,
                      float fullW, float pad)
{
    ImDrawList* dl     = ImGui::GetWindowDrawList();
    const float lineH  = ImGui::GetTextLineHeight();
    const float barH   = Theme::VolumeBarH;
    const float barW   = fullW - pad * 2.0f;

    // Label on its own line
    ImGui::SetCursorPosX(pad);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    // Bar on the next line
    ImGui::SetCursorPosX(pad);
    ImVec2 barPos = ImGui::GetCursorScreenPos();
    barPos.y += (lineH - barH) * 0.5f;

    // Background + border
    dl->AddRectFilled(barPos, { barPos.x + barW, barPos.y + barH }, Theme::U32SegOff);
    dl->AddRect      (barPos, { barPos.x + barW, barPos.y + barH }, Theme::U32Border, 0.f, 0, 1.f);

    // Segmented fill from center outward
    constexpr float segW = 4.0f, gap = 1.0f, step = segW + gap;
    const int total  = (int)((barW - 2.0f) / step);
    const int center = total / 2;
    const int filled = (int)((db + 12.0f) / 24.0f * total + 0.5f);

    for (int i = 0; i < total; ++i) {
        const float x0 = barPos.x + 1.0f + i * step;
        const float y0 = barPos.y + 2.0f;
        const float x1 = x0 + segW;
        const float y1 = barPos.y + barH - 2.0f;
        ImU32 col;
        if (filled > center)        col = (i >= center && i < filled) ? Theme::U32Accent    : Theme::U32SegOff;
        else if (filled < center)   col = (i >= filled && i < center) ? Theme::U32AccentDim : Theme::U32SegOff;
        else                        col = (i == center)                ? Theme::U32AccentDim : Theme::U32SegOff;
        dl->AddRectFilled({ x0, y0 }, { x1, y1 }, col);
    }

    // Centre notch
    if (center >= 0 && center < total) {
        const float cx = barPos.x + 1.0f + center * step;
        dl->AddRectFilled({ cx, barPos.y }, { cx + segW, barPos.y + barH }, Theme::U32Border);
    }

    // Invisible interaction button (full lineH height for easy clicking)
    ImGui::SetCursorScreenPos({ barPos.x, barPos.y - (lineH - barH) * 0.5f });
    ImGui::InvisibleButton(id, { barW, lineH });
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - barPos.x) / barW, 0.0f, 1.0f);
        db      = std::round(t * 24.0f - 12.0f);
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%+.0f dB", db);

    return changed;
}

// ---- OnUI -------------------------------------------------------------------

void SoundPanel::OnUI() {
    const float fullW  = ImGui::GetContentRegionAvail().x;
    const float pad    = Theme::WindowPadX;
    const float innerW = fullW - pad * 2.0f;

    ImGui::Spacing();

    // ---- Spatial preset buttons (Off / Room / Hall / Open) ------------------
    {
        const float btnSp = ImGui::GetStyle().ItemSpacing.x;
        const float btnW  = (innerW - 3.0f * btnSp) / 4.0f;
        const ImVec2 sz   = { btnW, Theme::TransportBtnH };
        ImGui::SetCursorPosX(pad);

        struct { int id; const char* label; } presets[] = {
            { 0, "Off"  },
            { 1, "Room" },
            { 2, "Hall" },
            { 3, "Open" },
        };
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine();
            if (PresetBtn(presets[i].label, _spatialPreset == presets[i].id, sz)) {
                _spatialPreset = presets[i].id;
                AudioPlayer::SetSpatialPreset(
                    static_cast<AudioPlayer::SpatialPreset>(_spatialPreset));
            }
        }
    }

    // ---- Azimuth wheel (only when spatial is active) ------------------------
    if (_spatialPreset != 0) {
        ImGui::Spacing();
        constexpr float wheelR = 30.0f;   // compact — fits without scroll
        const ImVec2 blockTL   = ImGui::GetCursorScreenPos();
        const ImVec2 center    = { blockTL.x + fullW * 0.5f,
                                   blockTL.y + wheelR + 8.0f };
        ImGui::Dummy({ fullW, wheelR * 2.0f + 16.0f });
        if (DrawAzimuthWheel("##azimuth", _spatialAzimuth, center, wheelR))
            AudioPlayer::SetSpatialAzimuth(_spatialAzimuth);
    }

    ImGui::Spacing();

    // ---- Retro EQ bars ------------------------------------------------------
    bool eqChanged = false;
    eqChanged |= DrawEQBar("##bass",   "Bass", _bassDb,   fullW, pad);
    eqChanged |= DrawEQBar("##mid",    "Mid ", _midDb,    fullW, pad);
    eqChanged |= DrawEQBar("##treble", "Treb", _trebleDb, fullW, pad);

    if (eqChanged)
        AudioPlayer::SetEQBands(DbToLinear(_bassDb),
                                DbToLinear(_midDb),
                                DbToLinear(_trebleDb));

    ImGui::Spacing();

    // ---- Reset button -------------------------------------------------------
    {
        constexpr float btnW = 56.0f;
        ImGui::SetCursorPosX(pad + (innerW - btnW) * 0.5f);
        if (ImGui::Button("Reset", { btnW, Theme::TransportBtnH })) {
            _bassDb = _midDb = _trebleDb = 0.0f;
            AudioPlayer::SetEQBands(1.0f, 1.0f, 1.0f);
        }
    }
}
