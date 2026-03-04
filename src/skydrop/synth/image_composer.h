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
    std::vector<i32> harmonize;  ///< Additional simultaneous notes (empty = single note).
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

// ── Musical structure ────────────────────────────────────────────────────────

enum class SectionType : u8 {
    Intro, Verse, PreChorus, Chorus, Bridge, Drop, Breakdown, Outro
};

/// A structural section of the composition, inferred from image regions.
struct Section {
    SectionType type;
    f32 startBeat;
    f32 endBeat;
    f32 energy;           ///< [0,1] drives layer intensity.
    f32 avgBrightness;    ///< Section-local brightness.
    u32 regionIdx;        ///< Image vertical region index (for motif reuse).
    bool isBuild;         ///< Crescendo toward next section.
    bool isDrop;          ///< Sudden entrance after silence.
};

/// A short melodic phrase extracted from an image region.
struct Motif {
    std::vector<i32> intervals;   ///< Pitch intervals from section root (semitones).
    std::vector<f32> durations;   ///< Note durations in beats.
    std::vector<f32> velocities;  ///< Per-note velocity.
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

    // Image-derived timbre.
    std::vector<f32> harmonicProfile;   ///< Overtone amplitude weights (index 0 = fundamental).
    std::vector<f32> dynamicMap;        ///< Per-beat intensity from image brightness scan.
    f32 detuneAmount  = 0.003f;         ///< Oscillator detune (from color complexity).
    f32 noiseBlend    = 0.0f;           ///< Noise mixed into timbre (from texture roughness).
    f32 filterSweep   = 0.0f;           ///< Filter LFO depth (from texture regularity).

    // Musical structure.
    std::vector<Section> sections;      ///< Song sections (intro, verse, chorus, etc.).
    std::vector<Motif> motifs;          ///< One motif per unique image region.
    f32 portamentoRate = 0.0f;          ///< Melody glide speed (0 = none, 1 = slow).
    f32 leadChordiness = 0.0f;          ///< 0–1 how often lead plays chords vs single notes.
    f32 melodyRelease  = 0.3f;          ///< Melody voice release time in seconds.
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

    static std::vector<f32> BuildHarmonicProfile(const ImageAnalysis& a);
    static std::vector<f32> BuildDynamicMap(const ImageData& img,
        const std::vector<Section>& sections, f32 totalBeats);

    static std::vector<Section> BuildStructure(
        const ImageData& img, const ImageAnalysis& a, f32 totalBeats, CompStyle style);
    static Motif ExtractMotif(
        const ImageData& img, u32 x, u32 w,
        i32 root, const std::vector<i32>& scale, CompStyle style, u32& rng);

    static std::vector<ChordEvent> BuildChords(
        const ImageData& img, const ImageAnalysis& a,
        i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style);

    static std::vector<NoteEvent> BuildMelody(
        const ImageData& img, const ImageAnalysis& a,
        const std::vector<ChordEvent>& chords,
        i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style);

    static std::vector<NoteEvent> BuildBass(
        const ImageData& img, const ImageAnalysis& a,
        const std::vector<ChordEvent>& chords,
        i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style);

    static std::vector<DrumHit> BuildDrums(
        const ImageData& img, const ImageAnalysis& a, f32 totalBeats, CompStyle style);
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
        f32      targetFreq = 0;    ///< Portamento target.
        f32      glideRate = 0;     ///< Per-sample freq smoothing.
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
    static constexpr u32 kMelodyVoices = 6;
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
