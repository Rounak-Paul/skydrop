/// @file transport.cpp
/// @brief Transport controls and metering UI.

#include "skydrop/ui/transport.h"
#include <imgui.h>

namespace sky {

void Transport::Draw() {
    if (!engine_) return;

    ImGui::Begin("Transport");

    // Playback controls.
    bool playing = engine_->IsPlaying();

    if (!playing) {
        if (ImGui::Button("  Play  ")) engine_->Play();
    } else {
        if (ImGui::Button(" Pause ")) engine_->Pause();
    }
    ImGui::SameLine();
    if (ImGui::Button("  Stop  ")) engine_->Stop();

    ImGui::SameLine();
    ImGui::Text("  %u Hz  |  Buffer: %u",
        engine_->GetSampleRate(), engine_->GetBufferSize());

    // Level meters.
    auto [peakL, peakR] = engine_->GetPeakLevel();
    ImGui::Spacing();
    float meterW = ImGui::GetContentRegionAvail().x;

    // Left channel.
    ImGui::Text("L");
    ImGui::SameLine();
    ImGui::ProgressBar(peakL, ImVec2(meterW - 30, 14));

    // Right channel.
    ImGui::Text("R");
    ImGui::SameLine();
    ImGui::ProgressBar(peakR, ImVec2(meterW - 30, 14));

    // Waveform.
    ImGui::Spacing();
    ImGui::Text("Output Waveform");
    WaveformView::DrawStereo(engine_->GetLastOutputBuffer());

    // Spectrum.
    ImGui::Spacing();
    ImGui::Text("Spectrum");
    SpectrumView::Draw(engine_->GetLastOutputBuffer(), engine_->GetSampleRate());

    ImGui::End();
}

} // namespace sky
