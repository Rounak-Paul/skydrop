#include "queue_panel.h"

#include "theme.h"
#include "music_events.h"
#include "audio/audio_player.h"
#include "job_system.h"

#include <tinyvk/tinyvk.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <imgui.h>

#include <cstdio>
#include <string>

// ---- Statics ------------------------------------------------------------

ListenerID QueuePanel::_idQueueChanged = 0;
std::vector<QueueChangedEvent::Entry> QueuePanel::_tracks;
int32_t QueuePanel::_currentIndex = -1;

std::unordered_map<std::string, QueuePanel::ThumbEntry> QueuePanel::_thumbs;
std::mutex                                               QueuePanel::_thumbsMutex;
std::vector<QueuePanel::PendingUpload>                   QueuePanel::_pendingUploads;
std::mutex                                               QueuePanel::_pendingMutex;

static constexpr float kThumbSz  = 46.0f; // thumbnail square size
static constexpr float kRowPadY  =  6.0f; // vertical padding above & below thumbnail

// ---- Thumbnail helpers --------------------------------------------------

void QueuePanel::ScheduleThumbnail(const std::string& path) {
    {
        std::lock_guard<std::mutex> lk(_thumbsMutex);
        if (_thumbs.count(path)) return; // already cached or pending
        _thumbs[path].state = ThumbState::Pending;
    }
    // Copy path into lambda so the background job owns it
    JobSystem::Submit([path]() {
        std::vector<uint8_t> pixels;
        int w = 0, h = 0;
        bool ok = AudioPlayer::ExtractArt(path, pixels, w, h);

        if (ok) {
            std::lock_guard<std::mutex> lk(_pendingMutex);
            _pendingUploads.push_back({ path, std::move(pixels), w, h });
        } else {
            std::lock_guard<std::mutex> lk(_thumbsMutex);
            _thumbs[path].state = ThumbState::NoArt;
        }
    });
}

void QueuePanel::FlushPendingUploads() {
    std::vector<PendingUpload> batch;
    {
        std::lock_guard<std::mutex> lk(_pendingMutex);
        if (_pendingUploads.empty()) return;
        batch.swap(_pendingUploads);
    }

    tvk::Renderer* renderer = tvk::App::Get()->GetRenderer();
    if (!renderer) return;

    for (auto& up : batch) {
        auto tex = tvk::Texture::Create(
            renderer, up.pixels.data(),
            static_cast<tvk::u32>(up.w), static_cast<tvk::u32>(up.h),
            tvk::TextureSpec{ .generateMipmaps = false });
        if (tex) tex->BindToImGui();

        std::lock_guard<std::mutex> lk(_thumbsMutex);
        auto& e = _thumbs[up.path];
        e.tex   = std::move(tex);
        e.texW  = up.w;
        e.texH  = up.h;
        e.state = ThumbState::Loaded;
    }
}

void QueuePanel::ClearThumbs() {
    {
        std::lock_guard<std::mutex> lk(_pendingMutex);
        _pendingUploads.clear();
    }
    std::lock_guard<std::mutex> lk(_thumbsMutex);
    _thumbs.clear();
}

// ---- Init / Shutdown ----------------------------------------------------

void QueuePanel::Init() {
    _idQueueChanged = Event::Register<QueueChangedEvent>([](const QueueChangedEvent& e) {
        _tracks       = e.tracks;
        _currentIndex = e.currentIndex;
        // Schedule thumbnail loading for any new paths
        for (const auto& t : e.tracks)
            ScheduleThumbnail(t.path);
    });
}

void QueuePanel::Shutdown() {
    Event::Unregister<QueueChangedEvent>(_idQueueChanged);
    ClearThumbs();
}

// ---- OnUI ---------------------------------------------------------------

void QueuePanel::OnUI() {
    // Upload any completed thumbnail jobs before rendering
    FlushPendingUploads();

    const float availW = ImGui::GetContentRegionAvail().x;

    // ---- Toolbar --------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Button,        Theme::BtnNormal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BtnHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::BtnActive);
    if (ImGui::Button(ICON_FA_TRASH " Clear")) {
        Event::Emit(QueueClearEvent{});
        ClearThumbs();
    }
    ImGui::PopStyleColor(3);

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::SameLine(availW - ImGui::CalcTextSize("000 tracks").x);
    if (!_tracks.empty()) {
        char cnt[32];
        std::snprintf(cnt, sizeof(cnt), "%zu track%s",
                      _tracks.size(), _tracks.size() == 1 ? "" : "s");
        ImGui::TextUnformatted(cnt);
    }
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Separator, Theme::Border);
    ImGui::Separator();
    ImGui::PopStyleColor();

    if (_tracks.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        float tw = ImGui::CalcTextSize("Open files via  File  menu").x;
        ImGui::SetCursorPosX((availW - tw) * 0.5f);
        ImGui::TextUnformatted(ICON_FA_MUSIC "  Open files via  File  menu");
        ImGui::PopStyleColor();
        return;
    }

    // ---- Track list -----------------------------------------------------
    const ImVec2 listSize = { 0.0f, ImGui::GetContentRegionAvail().y };
    if (!ImGui::BeginChild("##tracklist", listSize,
                           ImGuiChildFlags_AlwaysUseWindowPadding,
                           ImGuiWindowFlags_NoScrollbar)) {
        ImGui::EndChild();
        return;
    }

    const ImGuiStyle& style   = ImGui::GetStyle();
    const float childW         = ImGui::GetContentRegionAvail().x;
    const float rowH           = kThumbSz + kRowPadY * 2.0f;  // thumbnail + equal padding top/bottom
    const float lineH          = ImGui::GetTextLineHeight();
    const float dotW           = ImGui::CalcTextSize(ICON_FA_CIRCLE_DOT).x;

    int32_t removeIdx = -1;

    for (int32_t i = 0; i < static_cast<int32_t>(_tracks.size()); ++i) {
        const auto& t         = _tracks[i];
        const bool  isCurrent = (i == _currentIndex);

        ImGui::PushID(i);

        // Capture row origin BEFORE the invisible button
        const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();

        // Full-row hit target — advances cursor by rowH
        ImGui::InvisibleButton("##row", { childW, rowH });
        if (ImGui::IsItemClicked())
            Event::Emit(PlayTrackEvent{ i });

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ---- Background highlight for current track -------------------
        if (isCurrent) {
            ImVec2 bgMin = { rowOrigin.x - style.WindowPadding.x, rowOrigin.y };
            ImVec2 bgMax = { bgMin.x + ImGui::GetWindowWidth(), rowOrigin.y + rowH };
            dl->AddRectFilled(bgMin, bgMax, IM_COL32(80, 53, 0, 80));
        }
        // Hover tint
        if (ImGui::IsItemHovered() && !isCurrent)
            dl->AddRectFilled(rowOrigin, { rowOrigin.x + childW, rowOrigin.y + rowH },
                              IM_COL32(80, 53, 0, 30));

        // ---- Thumbnail (centre-crop, rounded) -------------------------
        // Drawn with kRowPadY padding at top
        const ImVec2 tMin = { rowOrigin.x, rowOrigin.y + kRowPadY };
        const ImVec2 tMax = { tMin.x + kThumbSz, tMin.y + kThumbSz };
        {
            std::lock_guard<std::mutex> lk(_thumbsMutex);
            auto it = _thumbs.find(t.path);
            if (it != _thumbs.end() &&
                it->second.state == ThumbState::Loaded &&
                it->second.tex) {
                // Centre-crop UVs so the image fills the square (cover-fit)
                float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
                const int tw = it->second.texW, th = it->second.texH;
                if (tw > 0 && th > 0 && tw != th) {
                    if (tw > th) {
                        float c = (float)(tw - th) / (2.f * tw);
                        u0 = c; u1 = 1.f - c;
                    } else {
                        float c = (float)(th - tw) / (2.f * th);
                        v0 = c; v1 = 1.f - c;
                    }
                }
                dl->AddImageRounded(
                    reinterpret_cast<ImTextureID>(it->second.tex->GetImGuiTextureID()),
                    tMin, tMax, { u0, v0 }, { u1, v1 },
                    IM_COL32_WHITE, 4.0f);
            } else {
                // Placeholder
                dl->AddRectFilled(tMin, tMax, IM_COL32(30, 22, 5, 200), 4.0f);
                dl->AddRect(tMin, tMax, Theme::U32Border, 4.0f, 0, 1.0f);
                const char* ico = ICON_FA_MUSIC;
                ImVec2 icoSz = ImGui::CalcTextSize(ico);
                dl->AddText(nullptr, 0.f,
                    { tMin.x + (kThumbSz - icoSz.x) * 0.5f,
                      tMin.y + (kThumbSz - icoSz.y) * 0.5f },
                    isCurrent ? Theme::U32Accent : IM_COL32(80, 65, 40, 160), ico);
            }
        }

        // ---- Text area ------------------------------------------------
        // Layout: [thumb] gap [dot?] gap [title / artist] ... [dur]
        const float textX     = tMax.x + style.ItemSpacing.x;
        const float rightEdge = rowOrigin.x + childW;

        // Duration string (drawn at right)
        char dur[12] = {};
        if (t.durationSeconds > 0.f) {
            int m = (int)t.durationSeconds / 60;
            int s = (int)t.durationSeconds % 60;
            std::snprintf(dur, sizeof(dur), "%d:%02d", m, s);
        }
        const ImVec2 durSz     = ImGui::CalcTextSize(dur);
        const float  durX      = rightEdge - durSz.x - 2.f;
        // Text block must not overlap duration; leave a small gap
        const float  textMaxX  = (dur[0] ? durX - 6.f : rightEdge);

        // Two-line text block, vertically centred in full rowH
        const bool   hasArtist   = !t.artist.empty();
        const float  lineSpacing = 3.f;
        const float  blockH      = hasArtist ? (lineH * 2.f + lineSpacing) : lineH;
        const float  y0          = rowOrigin.y + (rowH - blockH) * 0.5f;

        // Playing indicator dot — aligned with title line (y0)
        float afterDot = textX;
        if (isCurrent) {
            dl->AddText(nullptr, 0.f, { textX, y0 },
                        Theme::U32AccentHot, ICON_FA_CIRCLE_DOT);
            afterDot = textX + dotW + style.ItemSpacing.x;
        }

        // Build title string (fall back to filename)
        const std::string titleStr = !t.title.empty() ? t.title : [&]() {
            const size_t slash = t.path.find_last_of("/\\");
            return (slash != std::string::npos) ? t.path.substr(slash + 1) : t.path;
        }();

        // Clip text to textMaxX
        dl->PushClipRect({ afterDot, rowOrigin.y },
                         { textMaxX, rowOrigin.y + kThumbSz }, true);

        dl->AddText(nullptr, 0.f, { afterDot, y0 },
            isCurrent ? Theme::U32AccentHot : Theme::U32TextPrimary,
            titleStr.c_str());

        if (hasArtist) {
            dl->AddText(nullptr, 0.f,
                { afterDot, y0 + lineH + lineSpacing },
                Theme::U32TextDim, t.artist.c_str());
        }

        dl->PopClipRect();

        // Duration — vertically centred in rowH
        if (dur[0]) {
            dl->AddText(nullptr, 0.f,
                { durX, rowOrigin.y + (rowH - durSz.y) * 0.5f },
                Theme::U32TextDim, dur);
        }

        // Thin separator at row bottom
        dl->AddLine(
            { rowOrigin.x, rowOrigin.y + rowH - 0.5f },
            { rowOrigin.x + childW, rowOrigin.y + rowH - 0.5f },
            IM_COL32(60, 45, 15, 50), 1.f);

        // Right-click context menu (operates on the InvisibleButton above)
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem(ICON_FA_PLAY  "  Play"))
                Event::Emit(PlayTrackEvent{ i });
            if (ImGui::MenuItem(ICON_FA_TRASH "  Remove"))
                removeIdx = i;
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    if (removeIdx >= 0) Event::Emit(RemoveTrackEvent{ removeIdx });

    ImGui::EndChild();
}

