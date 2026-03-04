#pragma once

/// @file transport.h
/// @brief Playback transport controls UI (play, pause, stop, metering).

#include "skydrop/audio/audio_engine.h"
#include "skydrop/ui/waveform_view.h"
#include "skydrop/ui/spectrum_view.h"

namespace sky {

/// Draws transport controls and level meters.
class Transport {
public:
    /// Set the audio engine to control.
    void SetEngine(AudioEngine* engine) { engine_ = engine; }

    /// Draw the transport bar. Call from OnUI().
    void Draw();

private:
    AudioEngine* engine_ = nullptr;
};

} // namespace sky
