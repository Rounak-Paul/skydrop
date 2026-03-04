#pragma once

/// @file skydrop_app.h
/// @brief Skydrop — a simple image-to-music player.
/// Load an image, pick an algorithm, tweak knobs, and listen.

#include "skydrop/core/audio_graph.h"
#include "skydrop/audio/audio_engine.h"
#include "skydrop/ui/image_panel.h"

// Synth algorithms
#include "skydrop/synth/spectral_mapper.h"
#include "skydrop/synth/color_harmony.h"
#include "skydrop/synth/texture_rhythm.h"
#include "skydrop/synth/edge_melody.h"
#include "skydrop/synth/region_pad.h"

// Effects
#include "skydrop/dsp/reverb.h"
#include "skydrop/dsp/mixer.h"

#include <tinyvk/tinyvk.h>

namespace sky {

/// Available composition styles.
enum class Algorithm : i32 {
    Ethereal = 0,
    Harmonic,
    Rhythmic,
    Melodic,
    Cinematic,
    Count
};

/// Human-readable style names.
inline const char* AlgorithmName(Algorithm a) {
    switch (a) {
    case Algorithm::Ethereal:  return "Ethereal";
    case Algorithm::Harmonic:  return "Harmonic";
    case Algorithm::Rhythmic:  return "Rhythmic";
    case Algorithm::Melodic:   return "Melodic";
    case Algorithm::Cinematic: return "Cinematic";
    default: return "Unknown";
    }
}

/// Short description per style.
inline const char* AlgorithmDescription(Algorithm a) {
    switch (a) {
    case Algorithm::Ethereal:  return "Slow, shimmering pads with gentle melody";
    case Algorithm::Harmonic:  return "Rich chord arpeggios and warm harmonies";
    case Algorithm::Rhythmic:  return "Driving beat with electronic bass and synth";
    case Algorithm::Melodic:   return "Flowing melodic lines over soft chords";
    case Algorithm::Cinematic: return "Dramatic atmosphere with deep bass swells";
    default: return "";
    }
}

class SkydropApp : public tvk::App {
protected:
    void OnStart() override;
    void OnUpdate() override;
    void OnUI() override;
    void OnStop() override;

private:
    void OpenImage();
    void BuildGraph();
    void ApplyImage();
    void DrawImageSection();
    void DrawControlsSection();
    void DrawSynthKnobs();
    void DrawMasterSection();
    void DrawTransportBar();
    void DrawVisualizerSection();

    // Audio pipeline (internal, user never sees the graph).
    AudioGraph   graph_;
    AudioEngine  engine_;
    ImagePanel   imagePanel_;

    // Current state.
    Algorithm    currentAlgo_  = Algorithm::Ethereal;
    Algorithm    builtAlgo_    = Algorithm::Ethereal;
    bool         graphBuilt_   = false;
    bool         autoPlay_     = true;
    bool         pendingApply_ = false; ///< True when waiting for async analysis.

    // Node IDs in the internal graph.
    NodeId       synthId_  = 0;
    NodeId       reverbId_ = 0;
    NodeId       outputId_ = 0;

    // Master controls.
    f32 masterVolume_ = 0.8f;
    f32 reverbMix_    = 0.3f;
    f32 reverbRoom_   = 0.7f;
};

} // namespace sky
