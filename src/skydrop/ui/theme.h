#pragma once

#include <imgui.h>

// ============================================================================
//  Skydrop Retro Theme — amber phosphor CRT palette
//  All UI colours, sizes, and spacing live here.  Do NOT hard-code anything
//  in the panel .cc files; reference these constants instead.
// ============================================================================

namespace Theme {

// ---- Colour palette (ImVec4 for ImGui style slots) -------------------------

// Backgrounds
static constexpr ImVec4 BgApp        = { 0.047f, 0.035f, 0.008f, 1.00f }; // #0C0902 — outermost shell
static constexpr ImVec4 BgPanel      = { 0.063f, 0.047f, 0.012f, 1.00f }; // #100C03 — window/panel bg
static constexpr ImVec4 BgChild      = { 0.031f, 0.024f, 0.004f, 1.00f }; // #080601 — child / inner area
static constexpr ImVec4 BgPopup      = { 0.078f, 0.059f, 0.016f, 0.96f }; // #140F04 — popup/dropdown bg

// Accent
static constexpr ImVec4 Accent       = { 0.800f, 0.533f, 0.000f, 1.00f }; // #CC8800
static constexpr ImVec4 AccentHot    = { 1.000f, 0.800f, 0.250f, 1.00f }; // #FFCC40
static constexpr ImVec4 AccentDim    = { 0.314f, 0.208f, 0.000f, 1.00f }; // #503500

// Text
static constexpr ImVec4 TextPrimary  = { 0.910f, 0.722f, 0.251f, 1.00f }; // #E8B840
static constexpr ImVec4 TextDim      = { 0.376f, 0.275f, 0.075f, 1.00f }; // #604613
static constexpr ImVec4 TextBright   = { 1.000f, 0.906f, 0.600f, 1.00f }; // #FFE799

// Borders
static constexpr ImVec4 Border       = { 0.200f, 0.140f, 0.020f, 1.00f }; // #332405
static constexpr ImVec4 BorderHot    = { 0.500f, 0.353f, 0.000f, 1.00f }; // #805A00

// Buttons
static constexpr ImVec4 BtnNormal    = { 0.078f, 0.055f, 0.008f, 1.00f }; // #140E02
static constexpr ImVec4 BtnHovered   = { 0.188f, 0.133f, 0.020f, 1.00f }; // #302205
static constexpr ImVec4 BtnActive    = { 0.400f, 0.263f, 0.000f, 1.00f }; // #664300
static constexpr ImVec4 BtnActiveHot = { 0.600f, 0.400f, 0.000f, 1.00f }; // #996600

// Scrollbar / grab
static constexpr ImVec4 Grab         = { 0.600f, 0.400f, 0.000f, 1.00f }; // #996600
static constexpr ImVec4 GrabHot      = { 0.800f, 0.533f, 0.000f, 1.00f }; // #CC8800

// Tabs
static constexpr ImVec4 TabInactive  = { 0.063f, 0.047f, 0.012f, 1.00f }; // same as BgPanel
static constexpr ImVec4 TabHovered   = { 0.180f, 0.125f, 0.024f, 1.00f }; // #2E2006
static constexpr ImVec4 TabActive    = { 0.118f, 0.082f, 0.016f, 1.00f }; // #1E1504

// Header (Selectable highlight)
static constexpr ImVec4 HeaderNormal = { 0.200f, 0.133f, 0.020f, 0.50f };
static constexpr ImVec4 HeaderHot    = { 0.350f, 0.233f, 0.030f, 0.80f };
static constexpr ImVec4 HeaderActive = { 0.500f, 0.333f, 0.000f, 1.00f };

// ---- ImU32 versions for ImDrawList ----------------------------------------

static constexpr ImU32 U32BgApp        = IM_COL32( 12,   9,   2, 255);
static constexpr ImU32 U32BgPanel      = IM_COL32( 16,  12,   3, 255);
static constexpr ImU32 U32BgChild      = IM_COL32(  8,   6,   1, 255);
static constexpr ImU32 U32Accent       = IM_COL32(204, 136,   0, 255);
static constexpr ImU32 U32AccentHot    = IM_COL32(255, 204,  64, 255);
static constexpr ImU32 U32AccentDim    = IM_COL32( 80,  53,   0, 255);
static constexpr ImU32 U32SegOff       = IM_COL32( 24,  16,   2, 255); // unlit segment
static constexpr ImU32 U32Border       = IM_COL32( 51,  36,   5, 255);
static constexpr ImU32 U32BorderHot    = IM_COL32(128,  90,   0, 255);
static constexpr ImU32 U32TextPrimary  = IM_COL32(232, 184,  64, 255);
static constexpr ImU32 U32TextDim      = IM_COL32( 96,  70,  19, 255);
static constexpr ImU32 U32ArtBorder    = IM_COL32( 80,  53,   0, 200);
static constexpr ImU32 U32ArtOverlay   = IM_COL32(  4,   3,   1, 160); // scanline tint
static constexpr ImU32 U32ArtScanline  = IM_COL32(  0,   0,   0,  35); // per-line shade
static constexpr ImU32 U32ArtAmberTint = IM_COL32(180, 100,   0,  55); // warm amber cast
static constexpr ImU32 U32ArtVignette  = IM_COL32(  0,   0,   0, 180); // corner fade
static constexpr ImU32 U32NowPlaying   = IM_COL32(255, 210,  80, 255); // active track

// ---- Layout constants (in pixels, before DPI) -----------------------------

static constexpr float MenuBarH         = 20.0f;
static constexpr float TabBarH          = 22.0f;
static constexpr float TransportBtnW    = 36.0f;
static constexpr float TransportBtnH    = 22.0f;
static constexpr float ToggleBtnW       = 32.0f;    // icon-only toggle
static constexpr float StatusBarH       = 14.0f;
static constexpr float SeekBarH         = 10.0f;
static constexpr float WaveformH        = 30.0f;    // waveform seek widget
static constexpr float VolumeBarH       = 10.0f;
static constexpr float ArtMaxFrac       = 0.72f;    // fraction of content width
static constexpr float ArtMinPx         = 100.0f;
static constexpr float ArtMaxPx         = 200.0f;
static constexpr float WindowPadX       = 8.0f;
static constexpr float WindowPadY       = 6.0f;

// ---- Retro custom widgets -------------------------------------------------

// Draw a segmented LED bar (like an old VU meter).
// `pos`  — top-left corner in screen space.
// `size` — total bounding box.
// `t`    — fill fraction [0,1].
// `segW` — width of each lit segment, gap = 1px.
inline void DrawSegBar(ImDrawList* dl, ImVec2 pos, ImVec2 size, float t, float segW = 4.0f) {
    // background track
    dl->AddRectFilled(pos, { pos.x + size.x, pos.y + size.y }, U32SegOff);
    dl->AddRect(pos, { pos.x + size.x, pos.y + size.y }, U32Border, 0.0f, 0, 1.0f);

    float gap     = 1.0f;
    float step    = segW + gap;
    int   total   = (int)((size.x - 2.0f) / step);
    int   filled  = (int)(t * total + 0.5f);

    for (int i = 0; i < total; ++i) {
        float x0 = pos.x + 1.0f + i * step;
        float y0 = pos.y + 2.0f;
        float x1 = x0 + segW;
        float y1 = pos.y + size.y - 2.0f;
        ImU32 col = (i < filled) ? U32Accent : U32SegOff;
        dl->AddRectFilled({ x0, y0 }, { x1, y1 }, col);
    }
}

// Draw scan-line overlay over a rect.
inline void DrawScanlines(ImDrawList* dl, ImVec2 tl, ImVec2 br) {
    for (float y = tl.y; y < br.y; y += 2.0f)
        dl->AddLine({ tl.x, y }, { br.x, y }, U32ArtScanline);
}

// Full retro CRT filter: scanlines + vertical grid lines + amber tint.
inline void DrawRetroFilter(ImDrawList* dl, ImVec2 tl, ImVec2 br) {
    // Horizontal scanlines every 2px
    for (float y = tl.y; y < br.y; y += 2.0f)
        dl->AddLine({ tl.x, y }, { br.x, y }, U32ArtScanline);

    // Vertical grid lines every 3px — together with scanlines makes a dot-matrix grid
    const ImU32 vline = IM_COL32(0, 0, 0, 22);
    for (float x = tl.x; x < br.x; x += 3.0f)
        dl->AddLine({ x, tl.y }, { x, br.y }, vline);

    // Amber colour tint
    dl->AddRectFilled(tl, br, U32ArtAmberTint);

    // Vignette — four gradient edges darkening toward the corners
    const float vw = (br.x - tl.x) * 0.45f;
    const float vh = (br.y - tl.y) * 0.45f;
    const ImU32 vig = U32ArtVignette;
    const ImU32 clr = IM_COL32(0, 0, 0, 0);
    // Left
    dl->AddRectFilledMultiColor({ tl.x,       tl.y }, { tl.x + vw,  br.y }, vig, clr, clr, vig);
    // Right
    dl->AddRectFilledMultiColor({ br.x - vw,  tl.y }, { br.x,       br.y }, clr, vig, vig, clr);
    // Top
    dl->AddRectFilledMultiColor({ tl.x, tl.y      }, { br.x, tl.y + vh  }, vig, vig, clr, clr);
    // Bottom
    dl->AddRectFilledMultiColor({ tl.x, br.y - vh }, { br.x, br.y       }, clr, clr, vig, vig);
}

// ---- Apply() — call once in OnStart before first frame --------------------

inline void Apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    // Geometry
    s.WindowRounding     = 0.0f;
    s.ChildRounding      = 0.0f;
    s.FrameRounding      = 0.0f;
    s.PopupRounding      = 0.0f;
    s.ScrollbarRounding  = 0.0f;
    s.GrabRounding       = 0.0f;
    s.TabRounding        = 0.0f;
    s.WindowBorderSize   = 1.0f;
    s.ChildBorderSize    = 1.0f;
    s.FrameBorderSize    = 1.0f;
    s.PopupBorderSize    = 1.0f;
    s.TabBorderSize      = 1.0f;
    s.WindowPadding      = { WindowPadX,  WindowPadY  };
    s.FramePadding       = { 6.0f,  3.0f };
    s.ItemSpacing        = { 6.0f,  4.0f };
    s.ItemInnerSpacing   = { 4.0f,  4.0f };
    s.CellPadding        = { 4.0f,  2.0f };
    s.IndentSpacing      = 12.0f;
    s.ScrollbarSize      = 8.0f;
    s.GrabMinSize        = 6.0f;
    s.TabBarOverlineSize = 0.0f;
    s.SeparatorTextBorderSize = 1.0f;

    ImVec4* c = s.Colors;

    c[ImGuiCol_Text]                  = TextPrimary;
    c[ImGuiCol_TextDisabled]          = TextDim;
    c[ImGuiCol_WindowBg]              = BgPanel;
    c[ImGuiCol_ChildBg]               = BgChild;
    c[ImGuiCol_PopupBg]               = BgPopup;
    c[ImGuiCol_Border]                = Border;
    c[ImGuiCol_BorderShadow]          = { 0,0,0,0 };
    c[ImGuiCol_FrameBg]               = BtnNormal;
    c[ImGuiCol_FrameBgHovered]        = BtnHovered;
    c[ImGuiCol_FrameBgActive]         = BtnActive;
    c[ImGuiCol_TitleBg]               = BgChild;
    c[ImGuiCol_TitleBgActive]         = BgChild;
    c[ImGuiCol_TitleBgCollapsed]      = BgChild;
    c[ImGuiCol_MenuBarBg]             = BgApp;
    c[ImGuiCol_ScrollbarBg]           = BgChild;
    c[ImGuiCol_ScrollbarGrab]         = Grab;
    c[ImGuiCol_ScrollbarGrabHovered]  = GrabHot;
    c[ImGuiCol_ScrollbarGrabActive]   = AccentHot;
    c[ImGuiCol_CheckMark]             = Accent;
    c[ImGuiCol_SliderGrab]            = Grab;
    c[ImGuiCol_SliderGrabActive]      = AccentHot;
    c[ImGuiCol_Button]                = BtnNormal;
    c[ImGuiCol_ButtonHovered]         = BtnHovered;
    c[ImGuiCol_ButtonActive]          = BtnActive;
    c[ImGuiCol_Header]                = HeaderNormal;
    c[ImGuiCol_HeaderHovered]         = HeaderHot;
    c[ImGuiCol_HeaderActive]          = HeaderActive;
    c[ImGuiCol_Separator]             = Border;
    c[ImGuiCol_SeparatorHovered]      = BorderHot;
    c[ImGuiCol_SeparatorActive]       = Accent;
    c[ImGuiCol_ResizeGrip]            = { 0,0,0,0 };
    c[ImGuiCol_ResizeGripHovered]     = AccentDim;
    c[ImGuiCol_ResizeGripActive]      = Accent;
    c[ImGuiCol_Tab]                   = TabInactive;
    c[ImGuiCol_TabHovered]            = TabHovered;
    c[ImGuiCol_TabSelected]           = TabActive;
    c[ImGuiCol_TabSelectedOverline]   = Accent;
    c[ImGuiCol_TabDimmed]             = TabInactive;
    c[ImGuiCol_TabDimmedSelected]     = TabActive;
    c[ImGuiCol_TabDimmedSelectedOverline] = AccentDim;
    c[ImGuiCol_DockingPreview]        = { Accent.x, Accent.y, Accent.z, 0.7f };
    c[ImGuiCol_DockingEmptyBg]        = BgApp;
    c[ImGuiCol_PlotLines]             = Accent;
    c[ImGuiCol_PlotLinesHovered]      = AccentHot;
    c[ImGuiCol_PlotHistogram]         = Accent;
    c[ImGuiCol_PlotHistogramHovered]  = AccentHot;
    c[ImGuiCol_TableHeaderBg]         = BgChild;
    c[ImGuiCol_TableBorderStrong]     = Border;
    c[ImGuiCol_TableBorderLight]      = BgChild;
    c[ImGuiCol_TableRowBg]            = { 0,0,0,0 };
    c[ImGuiCol_TableRowBgAlt]         = { 1,1,1,0.02f };
    c[ImGuiCol_TextLink]              = AccentHot;
    c[ImGuiCol_TextSelectedBg]        = { AccentDim.x, AccentDim.y, AccentDim.z, 0.6f };
    c[ImGuiCol_DragDropTarget]        = AccentHot;
    c[ImGuiCol_NavCursor]             = Accent;
    c[ImGuiCol_NavWindowingHighlight] = AccentHot;
    c[ImGuiCol_NavWindowingDimBg]     = { 0,0,0,0.5f };
    c[ImGuiCol_ModalWindowDimBg]      = { 0,0,0,0.6f };
}

} // namespace Theme
