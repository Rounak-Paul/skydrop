#pragma once

/// @file types.h
/// @brief Common types and constants for Skydrop.

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <algorithm>
#include <numeric>
#include <complex>
#include <unordered_map>
#include <optional>
#include <variant>
#include <cassert>

namespace sky {

// ---------------------------------------------------------------------------
// Numeric aliases
// ---------------------------------------------------------------------------
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using f32 = float;
using f64 = double;

// ---------------------------------------------------------------------------
// Smart-pointer aliases
// ---------------------------------------------------------------------------
template <typename T> using Scope = std::unique_ptr<T>;
template <typename T> using Ref   = std::shared_ptr<T>;
template <typename T> using Weak  = std::weak_ptr<T>;

template <typename T, typename... Args>
Scope<T> MakeScope(Args&&... args) { return std::make_unique<T>(std::forward<Args>(args)...); }

template <typename T, typename... Args>
Ref<T> MakeRef(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }

// ---------------------------------------------------------------------------
// Audio constants
// ---------------------------------------------------------------------------
constexpr u32  kDefaultSampleRate  = 44100;
constexpr u32  kDefaultBufferSize  = 1024;
constexpr u32  kDefaultChannels    = 2;
constexpr f32  kPI                 = 3.14159265358979323846f;
constexpr f32  kTwoPI              = 2.0f * kPI;
constexpr f32  kHalfPI             = kPI / 2.0f;

// ---------------------------------------------------------------------------
// Musical constants
// ---------------------------------------------------------------------------

/// MIDI note number to frequency (A4 = 440 Hz).
inline f32 MidiToFreq(f32 note) {
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

/// Frequency to MIDI note number.
inline f32 FreqToMidi(f32 freq) {
    return 69.0f + 12.0f * std::log2(freq / 440.0f);
}

/// Snap a frequency to the nearest note in a given scale.
/// @param freq The input frequency.
/// @param scaleIntervals Intervals in semitones from root (e.g. {0,2,4,5,7,9,11} for major).
/// @param rootMidi Root MIDI note (default C = 60).
inline f32 SnapToScale(f32 freq, const std::vector<i32>& scaleIntervals, i32 rootMidi = 60) {
    f32 midi = FreqToMidi(freq);
    f32 bestDist = 1000.0f;
    f32 bestMidi = midi;
    for (int octave = -2; octave <= 10; ++octave) {
        for (i32 interval : scaleIntervals) {
            f32 candidate = static_cast<f32>(rootMidi + octave * 12 + interval);
            f32 dist = std::abs(midi - candidate);
            if (dist < bestDist) {
                bestDist = dist;
                bestMidi = candidate;
            }
        }
    }
    return MidiToFreq(bestMidi);
}

/// Common scale definitions (semitone intervals from root).
namespace scales {
    inline const std::vector<i32> Major         = {0, 2, 4, 5, 7, 9, 11};
    inline const std::vector<i32> Minor         = {0, 2, 3, 5, 7, 8, 10};
    inline const std::vector<i32> Pentatonic    = {0, 2, 4, 7, 9};
    inline const std::vector<i32> MinorPenta    = {0, 3, 5, 7, 10};
    inline const std::vector<i32> Blues         = {0, 3, 5, 6, 7, 10};
    inline const std::vector<i32> Dorian        = {0, 2, 3, 5, 7, 9, 10};
    inline const std::vector<i32> Mixolydian    = {0, 2, 4, 5, 7, 9, 10};
    inline const std::vector<i32> Chromatic     = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    inline const std::vector<i32> WholeTone     = {0, 2, 4, 6, 8, 10};
    inline const std::vector<i32> Japanese      = {0, 1, 5, 7, 8};       // In scale
    inline const std::vector<i32> Arabic        = {0, 1, 4, 5, 7, 8, 11};
} // namespace scales

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

/// Linear interpolation.
inline f32 Lerp(f32 a, f32 b, f32 t) { return a + t * (b - a); }

/// Map value from one range to another.
inline f32 Map(f32 value, f32 inMin, f32 inMax, f32 outMin, f32 outMax) {
    return outMin + (value - inMin) / (inMax - inMin) * (outMax - outMin);
}

/// Clamp value.
inline f32 Clamp(f32 value, f32 lo, f32 hi) { return std::max(lo, std::min(hi, value)); }

/// Convert decibels to linear amplitude.
inline f32 DbToLin(f32 db) { return std::pow(10.0f, db / 20.0f); }

/// Convert linear amplitude to decibels.
inline f32 LinToDb(f32 lin) { return 20.0f * std::log10(std::max(lin, 1e-10f)); }

} // namespace sky
