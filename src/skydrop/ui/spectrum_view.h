#pragma once

/// @file spectrum_view.h
/// @brief Real-time spectrum analyzer display.

#include "skydrop/core/sample_buffer.h"
#include "skydrop/dsp/fft.h"

namespace sky {

/// Draws a real-time spectrum from audio samples.
class SpectrumView {
public:
    /// Draw the spectrum of a sample buffer. Call within an ImGui window.
    static void Draw(const SampleBuffer& buffer, u32 sampleRate = kDefaultSampleRate,
                     float width = 0, float height = 100.0f);
};

} // namespace sky
