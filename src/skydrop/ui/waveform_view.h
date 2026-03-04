#pragma once

/// @file waveform_view.h
/// @brief Real-time waveform display using ImGui.

#include "skydrop/core/sample_buffer.h"

namespace sky {

/// Draws a waveform from a SampleBuffer.
class WaveformView {
public:
    /// Draw the waveform. Call within an ImGui window.
    static void Draw(const SampleBuffer& buffer, float width = 0, float height = 80.0f);

    /// Draw a stereo waveform with L/R on top/bottom.
    static void DrawStereo(const SampleBuffer& buffer, float width = 0, float height = 120.0f);
};

} // namespace sky
