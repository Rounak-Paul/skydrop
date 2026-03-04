#pragma once

/// @file image_composer.h
/// @brief Complete music composition from image analysis.
/// Derives melody, harmony, bass, rhythm, and timbre from image features.
/// Produces structured, looping multi-layer music — not just modulated drones.

#include "skydrop/core/audio_node.h"
#include "skydrop/image/image_analyzer.h"

namespace sky {

// ── Musical event types ─────────────────────────────────────────────────────

enum class DrumType : u8 { Kick, Snare, HiHat, OpenHat };

struct NoteEvent {
    f32 startBeat;      ///< When the note starts (in beats from loop start).
    f32 duration;       ///< Duration in beats.
    i32 midiNote;       ///< MIDI note number.
    f32 velocity;       ///< 0–1 strength.
};

struct ChordEvent {
    f32 startBeat;
    f32 duration;
    std::vector<i32> midiNotes; ///< All simultaneous MIDI notes.
    f32 velocity;
};

struct DrumHit {
    f32 beat;           ///< Beat position.
    DrumType type;
    f32 velocity;
};

// ── Composition style ───────────────────────────────────────────────────────

enum class CompStyle : i32 {
    Ethereal = 0,       ///< Slow, ambient, pad-driven.
    Harmonic,           ///< Melodic pop, arpeggios, rich chords.
    Rhythmic,           ///< Beat-driven, electronic, punchy.
    Melodic,            ///< Flowing melody, supportive harmony.
    Cinematic,          ///< Big pads, sparse melody, dramatic.
    Count
};

// ── Composition plan ────────────────────────────────────────────────────────

/// A complete musical plan extracted from an image.
struct Composition {
    f32  tempo = 110.0f;            ///< BPM.
    i32  rootNote = 60;             ///< MIDI root (C4).
    const std::vector<i32>* scale = &scales::Major;
    f32  totalBeats = 32.0f;        ///< Loop length in beats.

    std::vector<NoteEvent>  melody;
    std::vector<ChordEvent> chords;
    std::vector<NoteEvent>  bass;
    std::vector<DrumHit>    drums;

    // Timbral parameters (0–1 range).
    f32 brightness   = 0.5f;        ///< Filter cutoff influence.
    f32 warmth       = 0.5f;        ///< Low-end richness.
    f32 melodyGain   = 0.30f;
    f32 chordGain    = 0.20f;
    f32 bassGain     = 0.25f;
    f32 drumGain     = 0.15f;
    f32 padAttack    = 0.3f;        ///< Chord voice attack in seconds.
    f32 melodyAttack = 0.01f;       ///< Melody voice attack in seconds.
};

// ── ImageComposer ───────────────────────────────────────────────────────────

/// Derives a full Composition from image data + analysis.
class ImageComposer {
public:
    static Composition Compose(const ImageData& image,
                               const ImageAnalysis& analysis,
                               CompStyle style);
private:
    static f32  DeriveTempo(const ImageAnalysis& a, CompStyle style);
    static i32  DeriveRootNote(const ImageAnalysis& a);
    static const std::vector<i32>* DeriveScale(const ImageAnalysis& a);
    static u32  SelectProgression(const ImageAnalysis& a, bool isMajor);

    static std::vector<ChordEvent> BuildChords(
        const ImageData& img, const ImageAnalysis& a,
        i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style);

    static std::vector<NoteEvent> BuildMelody(
        const ImageData& img, const ImageAnalysis& a,
        const std::vector<ChordEvent>& chords,
        i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style);

    static std::vector<NoteEvent> BuildBass(
        const std::vector<ChordEvent>& chords, f32 totalBeats, CompStyle style);

    static std::vector<DrumHit> BuildDrums(
        const ImageAnalysis& a, f32 totalBeats, CompStyle style);
};

// ── MusicRenderer ───────────────────────────────────────────────────────────

/// Renders a Composition into audio sample-by-sample.
/// Manages voice pools for melody, chords, bass, and drums.
class MusicRenderer {
public:
    void Init(u32 sampleRate);
    void SetComposition(Composition comp);
    void Process(SampleBuffer* output, u32 bufferSize);
    void Reset();

    /// External parameter overrides (applied in real-time by node params).
    void SetTempoScale(f32 s)    { tempoScale_ = s; }
    void SetBrightness(f32 b)    { brightness_ = b; }
    void SetEnergy(f32 e)        { energy_ = e; }
    void SetStereoWidth(f32 w)   { stereoWidth_ = w; }

private:
    // ── Voice types ─────────────────────────────────────────────────────
    enum EnvStage : u8 { Attack, Sustain, Release, Off };

    struct Voice {
        bool     active = false;
        f32      freq = 0;
        f32      amp = 0;
        f64      phase = 0;
        f64      phase2 = 0;    ///< Detuned 2nd oscillator.
        f32      envLevel = 0;
        EnvStage envStage = Off;
        f32      attackRate = 0;
        f32      releaseRate = 0;
        u32      samplesLeft = 0;   ///< Samples until release.
        f32      pan = 0;           ///< -1 left, +1 right.
        f32      filterState = 0;
    };

    struct DrumVoice {
        bool     active = false;
        DrumType type = DrumType::Kick;
        f32      env = 0;
        f64      phase = 0;
        f32      amp = 0;
        f32      filterState = 0;
    };

    // ── Voice pools ─────────────────────────────────────────────────────
    static constexpr u32 kMelodyVoices = 4;
    static constexpr u32 kChordVoices  = 12;
    static constexpr u32 kBassVoices   = 2;
    static constexpr u32 kDrumVoices   = 6;

    Voice     melodyVoices_[kMelodyVoices]{};
    Voice     chordVoices_[kChordVoices]{};
    Voice     bassVoices_[kBassVoices]{};
    DrumVoice drumVoices_[kDrumVoices]{};

    // ── Event tracking ──────────────────────────────────────────────────
    u32 nextMelody_ = 0;
    u32 nextChord_  = 0;
    u32 nextBass_   = 0;
    u32 nextDrum_   = 0;
    f32 prevBeat_   = -1.0f;

    // ── State ───────────────────────────────────────────────────────────
    Composition comp_;
    u64 totalSamples_ = 0;
    u32 sampleRate_   = 44100;
    u32 rng_          = 12345;  ///< Fast deterministic RNG for noise drums.

    // ── External param overrides ────────────────────────────────────────
    f32 tempoScale_   = 1.0f;
    f32 brightness_   = 0.5f;
    f32 energy_       = 0.5f;
    f32 stereoWidth_  = 0.7f;

    // ── Master filter state ─────────────────────────────────────────────
    f32 masterFilterL_ = 0;
    f32 masterFilterR_ = 0;

    // ── Helpers ─────────────────────────────────────────────────────────
    f32  BeatAt(u64 sample) const;
    void TriggerMelody(const NoteEvent& n);
    void TriggerChord(const ChordEvent& c);
    void TriggerBass(const NoteEvent& n);
    void TriggerDrum(const DrumHit& h);
    void UpdateVoice(Voice& v);
    f32  RenderMelodyVoice(Voice& v);
    void RenderChordVoice(Voice& v, f32& left, f32& right);
    f32  RenderBassVoice(Voice& v);
    f32  RenderDrumVoice(DrumVoice& v);
    f32  Noise();   ///< Deterministic white noise.
};

} // namespace sky
