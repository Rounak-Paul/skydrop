/// @file waveform_view.cpp
/// @brief ImGui waveform drawing.

#include "skydrop/ui/waveform_view.h"
#include <imgui.h>
#include <algorithm>

namespace sky {

void WaveformView::Draw(const SampleBuffer& buffer, float width, float height) {
    if (buffer.Empty()) return;

    if (width <= 0) width = ImGui::GetContentRegionAvail().x;

    auto canvasPos = ImGui::GetCursorScreenPos();
    auto* drawList = ImGui::GetWindowDrawList();

    // Background.
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + width, canvasPos.y + height),
        IM_COL32(30, 30, 35, 255));

    // Center line.
    float centerY = canvasPos.y + height * 0.5f;
    drawList->AddLine(
        ImVec2(canvasPos.x, centerY),
        ImVec2(canvasPos.x + width, centerY),
        IM_COL32(60, 60, 70, 255));

    // Draw waveform (channel 0).
    u32 frames = buffer.Frames();
    float samplesPerPixel = static_cast<float>(frames) / width;

    ImVec2 prev(canvasPos.x, centerY);
    for (float px = 0; px < width; px += 1.0f) {
        u32 sampleIdx = static_cast<u32>(px * samplesPerPixel);
        sampleIdx = std::min(sampleIdx, frames - 1);
        float val = buffer.Sample(0, sampleIdx);
        val = std::clamp(val, -1.0f, 1.0f);
        float y = centerY - val * height * 0.45f;
        ImVec2 curr(canvasPos.x + px, y);
        drawList->AddLine(prev, curr, IM_COL32(100, 220, 160, 220), 1.0f);
        prev = curr;
    }

    ImGui::Dummy(ImVec2(width, height));
}

void WaveformView::DrawStereo(const SampleBuffer& buffer, float width, float height) {
    if (buffer.Empty() || buffer.Channels() < 2) { Draw(buffer, width, height); return; }

    if (width <= 0) width = ImGui::GetContentRegionAvail().x;

    auto canvasPos = ImGui::GetCursorScreenPos();
    auto* drawList = ImGui::GetWindowDrawList();
    float halfH = height * 0.5f;

    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + width, canvasPos.y + height),
        IM_COL32(30, 30, 35, 255));

    u32 frames = buffer.Frames();
    float samplesPerPixel = static_cast<float>(frames) / width;

    // Left channel (top half).
    float centerL = canvasPos.y + halfH * 0.5f;
    drawList->AddLine(ImVec2(canvasPos.x, centerL),
        ImVec2(canvasPos.x + width, centerL), IM_COL32(60, 60, 70, 255));

    ImVec2 prevL(canvasPos.x, centerL);
    for (float px = 0; px < width; px += 1.0f) {
        u32 idx = std::min(static_cast<u32>(px * samplesPerPixel), frames - 1);
        float y = centerL - std::clamp(buffer.Sample(0, idx), -1.0f, 1.0f) * halfH * 0.4f;
        ImVec2 curr(canvasPos.x + px, y);
        drawList->AddLine(prevL, curr, IM_COL32(100, 220, 160, 220), 1.0f);
        prevL = curr;
    }

    // Right channel (bottom half).
    float centerR = canvasPos.y + halfH * 1.5f;
    drawList->AddLine(ImVec2(canvasPos.x, centerR),
        ImVec2(canvasPos.x + width, centerR), IM_COL32(60, 60, 70, 255));

    ImVec2 prevR(canvasPos.x, centerR);
    for (float px = 0; px < width; px += 1.0f) {
        u32 idx = std::min(static_cast<u32>(px * samplesPerPixel), frames - 1);
        float y = centerR - std::clamp(buffer.Sample(1, idx), -1.0f, 1.0f) * halfH * 0.4f;
        ImVec2 curr(canvasPos.x + px, y);
        drawList->AddLine(prevR, curr, IM_COL32(160, 120, 220, 220), 1.0f);
        prevR = curr;
    }

    // Divider.
    drawList->AddLine(
        ImVec2(canvasPos.x, canvasPos.y + halfH),
        ImVec2(canvasPos.x + width, canvasPos.y + halfH),
        IM_COL32(80, 80, 90, 255));

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace sky
