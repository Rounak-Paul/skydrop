#pragma once

/// @file skydrop.h
/// @brief Unified header for the Skydrop library.

// Core
#include "skydrop/core/types.h"
#include "skydrop/core/sample_buffer.h"
#include "skydrop/core/audio_node.h"
#include "skydrop/core/audio_graph.h"

// DSP
#include "skydrop/dsp/fft.h"
#include "skydrop/dsp/oscillator.h"
#include "skydrop/dsp/envelope.h"
#include "skydrop/dsp/filter.h"
#include "skydrop/dsp/delay.h"
#include "skydrop/dsp/reverb.h"
#include "skydrop/dsp/mixer.h"

// Image
#include "skydrop/image/image_loader.h"
#include "skydrop/image/image_analyzer.h"

// Synth (image-to-music algorithms)
#include "skydrop/synth/spectral_mapper.h"
#include "skydrop/synth/color_harmony.h"
#include "skydrop/synth/texture_rhythm.h"
#include "skydrop/synth/edge_melody.h"
#include "skydrop/synth/region_pad.h"

// Audio output
#include "skydrop/audio/audio_engine.h"

// UI
#include "skydrop/ui/waveform_view.h"
#include "skydrop/ui/spectrum_view.h"
#include "skydrop/ui/image_panel.h"

// App
#include "skydrop/app/skydrop_app.h"
