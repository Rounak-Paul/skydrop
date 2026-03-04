/// @file spectrum_view.cpp
/// @brief Spectrum analyzer with ImGui rendering.

#include "skydrop/ui/spectrum_view.h"
#include <imgui.h>
#include <cmath>

namespace sky {

void SpectrumView::Draw(const SampleBuffer& buffer, u32 sampleRate,
                        float width, float height) {
    if (buffer.Empty()) return;
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;

    auto canvasPos = ImGui::GetCursorScreenPos();
    auto* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + width, canvasPos.y + height),
        IM_COL32(30, 30, 35, 255));

    // Compute FFT on mono sum.
    u32 frames = buffer.Frames();
    u32 fftSize = FFT::NextPow2(frames);
    std::vector<FFT::Complex> fftData(fftSize, FFT::Complex(0, 0));

    for (u32 f = 0; f < frames; ++f) {
        float val = 0;
        for (u32 c = 0; c < buffer.Channels(); ++c) val += buffer.Sample(c, f);
        val /= static_cast<float>(buffer.Channels());
        // Apply Hann window.
        float window = 0.5f * (1.0f - std::cos(kTwoPI * static_cast<float>(f) / static_cast<float>(frames)));
        fftData[f] = FFT::Complex(val * window, 0);
    }

    FFT::Forward(fftData);
    auto mag = FFT::Magnitude(fftData);

    // Convert to dB and draw bars.
    u32 numBins = static_cast<u32>(mag.size());
    float binsPerPixel = static_cast<float>(numBins) / width;

    float prevY = canvasPos.y + height;
    for (float px = 0; px < width; px += 2.0f) {
        // Log-frequency mapping for more natural display.
        float t = px / width;
        u32 binIdx = static_cast<u32>(std::pow(t, 2.0f) * numBins);
        binIdx = std::min(binIdx, numBins - 1);

        float db = 20.0f * std::log10(std::max(mag[binIdx], 1e-10f));
        float normalized = (db + 80.0f) / 80.0f; // -80dB to 0dB → 0..1
        normalized = std::clamp(normalized, 0.0f, 1.0f);

        float barHeight = normalized * height * 0.9f;
        float barTop = canvasPos.y + height - barHeight;

        // Gradient color: green → yellow → red.
        u8 r = static_cast<u8>(std::min(255.0f, normalized * 2.0f * 255.0f));
        u8 g = static_cast<u8>(std::min(255.0f, (1.0f - normalized * 0.5f) * 220.0f));

        drawList->AddRectFilled(
            ImVec2(canvasPos.x + px, barTop),
            ImVec2(canvasPos.x + px + 2.0f, canvasPos.y + height),
            IM_COL32(r, g, 80, 200));
    }

    // Frequency labels.
    float freqs[] = {100, 500, 1000, 5000, 10000};
    for (float f : freqs) {
        float t = std::sqrt(f / (sampleRate * 0.5f));
        if (t >= 0 && t <= 1) {
            float px = t * width;
            drawList->AddLine(
                ImVec2(canvasPos.x + px, canvasPos.y),
                ImVec2(canvasPos.x + px, canvasPos.y + height),
                IM_COL32(80, 80, 90, 150));

            char label[16];
            if (f >= 1000) snprintf(label, sizeof(label), "%.0fk", f / 1000);
            else snprintf(label, sizeof(label), "%.0f", f);
            drawList->AddText(ImVec2(canvasPos.x + px + 2, canvasPos.y + 2),
                IM_COL32(140, 140, 150, 200), label);
        }
    }

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace sky
