/// @file image_composer.cpp
/// @brief Complete image-to-music composition engine.

#include "skydrop/synth/image_composer.h"
#include <algorithm>
#include <cmath>

namespace sky {

// ═══════════════════════════════════════════════════════════════════════════
// Chord progression templates
// ═══════════════════════════════════════════════════════════════════════════

struct ChordTemplate {
    i32 rootSemitone;               ///< Offset from key root in semitones.
    std::vector<i32> intervals;     ///< Intervals making up the chord.
};

using Progression = std::vector<ChordTemplate>;

/// Major-key progressions.
static const Progression kMajorProgs[] = {
    // I – V – vi – IV  (pop anthem)
    {{0, {0,4,7}}, {7, {0,4,7}}, {9, {0,3,7}}, {5, {0,4,7}}},
    // I – vi – IV – V  (classic)
    {{0, {0,4,7}}, {9, {0,3,7}}, {5, {0,4,7}}, {7, {0,4,7}}},
    // I – IV – V – IV  (bright)
    {{0, {0,4,7}}, {5, {0,4,7}}, {7, {0,4,7}}, {5, {0,4,7}}},
    // vi – IV – I – V  (modern pop)
    {{9, {0,3,7}}, {5, {0,4,7}}, {0, {0,4,7}}, {7, {0,4,7}}},
    // I – IV – vi – V  (lyrical)
    {{0, {0,4,7}}, {5, {0,4,7}}, {9, {0,3,7}}, {7, {0,4,7}}},
};

/// Minor-key progressions.
static const Progression kMinorProgs[] = {
    // i – VI – III – VII  (emotional)
    {{0, {0,3,7}}, {8, {0,4,7}}, {3, {0,4,7}}, {10, {0,4,7}}},
    // i – iv – VII – III  (dark)
    {{0, {0,3,7}}, {5, {0,3,7}}, {10, {0,4,7}}, {3, {0,4,7}}},
    // i – VII – VI – VII  (cinematic)
    {{0, {0,3,7}}, {10, {0,4,7}}, {8, {0,4,7}}, {10, {0,4,7}}},
    // i – iv – v – i  (haunting)
    {{0, {0,3,7}}, {5, {0,3,7}}, {7, {0,3,7}}, {0, {0,3,7}}},
};

constexpr u32 kNumMajorProgs = sizeof(kMajorProgs) / sizeof(kMajorProgs[0]);
constexpr u32 kNumMinorProgs = sizeof(kMinorProgs) / sizeof(kMinorProgs[0]);

// ═══════════════════════════════════════════════════════════════════════════
// Helper: snap MIDI note to nearest scale tone
// ═══════════════════════════════════════════════════════════════════════════

static i32 SnapMidiToScale(i32 midi, i32 root, const std::vector<i32>& scale) {
    i32 best = midi;
    i32 bestDist = 999;
    for (int oct = -2; oct <= 10; ++oct) {
        for (i32 iv : scale) {
            i32 candidate = root + oct * 12 + iv;
            i32 dist = std::abs(midi - candidate);
            if (dist < bestDist) { bestDist = dist; best = candidate; }
        }
    }
    return best;
}

// ═══════════════════════════════════════════════════════════════════════════
// ImageComposer
// ═══════════════════════════════════════════════════════════════════════════

Composition ImageComposer::Compose(const ImageData& image,
                                   const ImageAnalysis& analysis,
                                   CompStyle style)
{
    Composition c;

    c.tempo    = DeriveTempo(analysis, style);
    c.rootNote = DeriveRootNote(analysis);
    c.scale    = DeriveScale(analysis);

    // Loop length varies by style.
    switch (style) {
    case CompStyle::Ethereal:  c.totalBeats = 32; break;
    case CompStyle::Harmonic:  c.totalBeats = 32; break;
    case CompStyle::Rhythmic:  c.totalBeats = 16; break;
    case CompStyle::Melodic:   c.totalBeats = 32; break;
    case CompStyle::Cinematic: c.totalBeats = 32; break;
    default:                   c.totalBeats = 32; break;
    }

    c.chords = BuildChords(image, analysis, c.rootNote, *c.scale, c.totalBeats, style);
    c.melody = BuildMelody(image, analysis, c.chords, c.rootNote, *c.scale, c.totalBeats, style);
    c.bass   = BuildBass(c.chords, c.totalBeats, style);
    c.drums  = BuildDrums(analysis, c.totalBeats, style);

    // Timbral settings from image + style.
    c.brightness = analysis.color.brightness;
    c.warmth     = (analysis.color.warmth + 1.0f) * 0.5f; // map -1..1 → 0..1

    switch (style) {
    case CompStyle::Ethereal:
        c.melodyGain = 0.18f; c.chordGain = 0.25f; c.bassGain = 0.18f; c.drumGain = 0.04f;
        c.padAttack = 1.5f; c.melodyAttack = 0.3f;
        break;
    case CompStyle::Harmonic:
        c.melodyGain = 0.28f; c.chordGain = 0.22f; c.bassGain = 0.20f; c.drumGain = 0.10f;
        c.padAttack = 0.4f; c.melodyAttack = 0.02f;
        break;
    case CompStyle::Rhythmic:
        c.melodyGain = 0.22f; c.chordGain = 0.15f; c.bassGain = 0.28f; c.drumGain = 0.20f;
        c.padAttack = 0.05f; c.melodyAttack = 0.005f;
        break;
    case CompStyle::Melodic:
        c.melodyGain = 0.32f; c.chordGain = 0.20f; c.bassGain = 0.18f; c.drumGain = 0.08f;
        c.padAttack = 0.6f; c.melodyAttack = 0.01f;
        break;
    case CompStyle::Cinematic:
        c.melodyGain = 0.15f; c.chordGain = 0.30f; c.bassGain = 0.22f; c.drumGain = 0.05f;
        c.padAttack = 2.0f; c.melodyAttack = 0.5f;
        break;
    default: break;
    }

    return c;
}

// ── Derive tempo ────────────────────────────────────────────────────────────

f32 ImageComposer::DeriveTempo(const ImageAnalysis& a, CompStyle style) {
    // Base energy from edges + texture.
    f32 energy = a.edges.density * 0.5f + a.texture.roughness * 0.3f
               + a.color.brightness * 0.2f;

    // Style-dependent range.
    f32 lo, hi;
    switch (style) {
    case CompStyle::Ethereal:  lo = 55;  hi = 80;  break;
    case CompStyle::Harmonic:  lo = 85;  hi = 125; break;
    case CompStyle::Rhythmic:  lo = 110; hi = 145; break;
    case CompStyle::Melodic:   lo = 80;  hi = 115; break;
    case CompStyle::Cinematic: lo = 50;  hi = 75;  break;
    default:                   lo = 80;  hi = 130; break;
    }

    return lo + energy * (hi - lo);
}

// ── Derive key ──────────────────────────────────────────────────────────────

i32 ImageComposer::DeriveRootNote(const ImageAnalysis& a) {
    // Map dominant hue (0-360) to 12 pitch classes, then place in octave 3-4.
    // Hue circle → circle of fifths ordering for musical coherence.
    // 0°=C, 30°=G, 60°=D, 90°=A, 120°=E, 150°=B,
    // 180°=F#, 210°=Db, 240°=Ab, 270°=Eb, 300°=Bb, 330°=F
    static const i32 hueToPC[] = {0,7,2,9,4,11,6,1,8,3,10,5};
    i32 segment = static_cast<i32>(a.color.dominantHue / 30.0f) % 12;
    i32 pc = hueToPC[segment];

    // Place in octave 3 (MIDI 48-59) for a comfortable root.
    return 48 + pc;
}

// ── Derive scale ────────────────────────────────────────────────────────────

const std::vector<i32>* ImageComposer::DeriveScale(const ImageAnalysis& a) {
    // Warm images → major family, cool → minor family.
    // High complexity → modal, low complexity → pentatonic.
    bool warm = a.color.warmth > 0.0f;

    if (a.color.complexity < 0.3f) {
        return warm ? &scales::Pentatonic : &scales::MinorPenta;
    } else if (a.color.complexity > 0.65f) {
        return warm ? &scales::Mixolydian : &scales::Dorian;
    } else {
        return warm ? &scales::Major : &scales::Minor;
    }
}

// ── Select progression index ────────────────────────────────────────────────

u32 ImageComposer::SelectProgression(const ImageAnalysis& a, bool isMajor) {
    // Use hue + saturation as a pseudo-hash to deterministically pick a progression.
    f32 selector = std::fmod(a.color.dominantHue * 7.3f + a.color.saturation * 13.7f, 1.0f);
    u32 maxIdx = isMajor ? kNumMajorProgs : kNumMinorProgs;
    return static_cast<u32>(selector * maxIdx) % maxIdx;
}

// ── Build chord progression ─────────────────────────────────────────────────

std::vector<ChordEvent> ImageComposer::BuildChords(
    const ImageData& img, const ImageAnalysis& a,
    i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style)
{
    std::vector<ChordEvent> chords;
    bool isMajor = (a.color.warmth > 0.0f);
    u32 progIdx = SelectProgression(a, isMajor);

    const Progression& prog = isMajor ? kMajorProgs[progIdx] : kMinorProgs[progIdx];

    // Beats per chord.
    f32 beatsPerChord = totalBeats / static_cast<f32>(prog.size());

    // For Rhythmic style: repeat the 4-chord progression to fill 16 beats.
    // For others: spread across totalBeats.
    u32 numChords = static_cast<u32>(prog.size());
    if (style == CompStyle::Rhythmic && totalBeats >= 16) {
        numChords = static_cast<u32>(prog.size()) * 2;
        beatsPerChord = totalBeats / static_cast<f32>(numChords);
    }

    // Voicing octave adjustments by style.
    i32 voicingOctave = 0; // above root
    switch (style) {
    case CompStyle::Ethereal:  voicingOctave = 0; break;
    case CompStyle::Harmonic:  voicingOctave = 0; break;
    case CompStyle::Rhythmic:  voicingOctave = 0; break;
    case CompStyle::Melodic:   voicingOctave = -1; break;
    case CompStyle::Cinematic: voicingOctave = -1; break;
    default: break;
    }

    for (u32 i = 0; i < numChords; ++i) {
        auto& templ = prog[i % prog.size()];

        ChordEvent ev;
        ev.startBeat = static_cast<f32>(i) * beatsPerChord;
        ev.duration  = beatsPerChord;

        // Image brightness at this time position modulates velocity.
        f32 t = static_cast<f32>(i) / static_cast<f32>(numChords);
        u32 imgX = static_cast<u32>(t * img.Width());
        if (imgX >= img.Width()) imgX = img.Width() - 1;
        f32 bri = img.AverageBrightness(imgX, 0, std::max(1u, img.Width() / numChords), img.Height());
        ev.velocity = 0.4f + bri * 0.5f;

        // Build chord notes from template.
        i32 chordRoot = root + templ.rootSemitone + voicingOctave * 12;
        for (i32 iv : templ.intervals) {
            i32 note = chordRoot + iv;
            ev.midiNotes.push_back(note);
        }

        // For richer voicings, add octave doubling of root.
        if (style == CompStyle::Cinematic || style == CompStyle::Ethereal) {
            ev.midiNotes.push_back(chordRoot + 12);
        }

        // Add 7th for harmonic/melodic styles when complexity is high.
        if ((style == CompStyle::Harmonic || style == CompStyle::Melodic) && a.color.complexity > 0.5f) {
            // Minor 7th (10 semitones) for minor chords, major 7th (11) for major.
            bool isMinor = (templ.intervals.size() >= 2 && templ.intervals[1] == 3);
            ev.midiNotes.push_back(chordRoot + (isMinor ? 10 : 11));
        }

        chords.push_back(std::move(ev));
    }

    return chords;
}

// ── Build melody ────────────────────────────────────────────────────────────

std::vector<NoteEvent> ImageComposer::BuildMelody(
    const ImageData& img, const ImageAnalysis& a,
    const std::vector<ChordEvent>& chords,
    i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style)
{
    std::vector<NoteEvent> melody;
    if (!img.IsLoaded() || chords.empty()) return melody;

    // Style-dependent note density (beats per note).
    f32 noteLen;
    f32 restProb;   // probability of rest.
    i32 melodyOctave;
    switch (style) {
    case CompStyle::Ethereal:
        noteLen = 4.0f; restProb = 0.3f; melodyOctave = 12; break;
    case CompStyle::Harmonic:
        noteLen = 1.0f; restProb = 0.15f; melodyOctave = 12; break;
    case CompStyle::Rhythmic:
        noteLen = 0.5f; restProb = 0.25f; melodyOctave = 12; break;
    case CompStyle::Melodic:
        noteLen = 0.75f; restProb = 0.1f; melodyOctave = 12; break;
    case CompStyle::Cinematic:
        noteLen = 4.0f; restProb = 0.5f; melodyOctave = 24; break;
    default:
        noteLen = 1.0f; restProb = 0.15f; melodyOctave = 12; break;
    }

    // Trace brightness across top 1/3 of image as pitch contour.
    u32 numNotes = static_cast<u32>(totalBeats / noteLen);
    if (numNotes == 0) numNotes = 1;

    std::vector<f32> pitchContour(numNotes);
    u32 stripH = std::max(1u, img.Height() / 3);
    for (u32 i = 0; i < numNotes; ++i) {
        f32 t = static_cast<f32>(i) / static_cast<f32>(numNotes);
        u32 x = static_cast<u32>(t * img.Width());
        u32 w = std::max(1u, img.Width() / numNotes);
        if (x >= img.Width()) x = img.Width() - 1;
        pitchContour[i] = img.AverageBrightness(x, 0, w, stripH);
    }

    // Also use edge vertical profile for pitch guidance if available.
    if (!a.edges.verticalProfile.empty()) {
        for (u32 i = 0; i < numNotes; ++i) {
            u32 profIdx = i * static_cast<u32>(a.edges.verticalProfile.size()) / numNotes;
            if (profIdx < a.edges.verticalProfile.size()) {
                pitchContour[i] = pitchContour[i] * 0.6f + a.edges.verticalProfile[profIdx] * 0.4f;
            }
        }
    }

    // Deterministic pseudo-random for rests/variation (seeded from image).
    u32 rng = static_cast<u32>(a.color.dominantHue * 100 + a.color.saturation * 1000);

    i32 prevMidi = root + melodyOctave; // start on root
    for (u32 i = 0; i < numNotes; ++i) {
        rng = rng * 1664525u + 1013904223u;
        f32 rval = static_cast<f32>(rng & 0xFFFF) / 65535.0f;

        // Rest probability.
        if (rval < restProb) continue;

        // Target pitch from contour.
        f32 targetNorm = pitchContour[i]; // 0–1
        i32 targetMidi = root + melodyOctave + static_cast<i32>(targetNorm * 14.0f); // ~1 octave range

        // Prefer stepwise motion: limit jump from previous note.
        i32 maxJump = (style == CompStyle::Cinematic) ? 7 : 4;
        if (targetMidi > prevMidi + maxJump) targetMidi = prevMidi + maxJump;
        if (targetMidi < prevMidi - maxJump) targetMidi = prevMidi - maxJump;

        // On strong beats (beat 0, 4, 8,...), prefer chord tones.
        f32 beat = static_cast<f32>(i) * noteLen;
        bool strongBeat = (std::fmod(beat, 4.0f) < 0.01f);

        if (strongBeat && !chords.empty()) {
            // Find current chord.
            const ChordEvent* curChord = &chords[0];
            for (auto& ch : chords) {
                if (ch.startBeat <= beat) curChord = &ch;
            }
            // Find nearest chord tone.
            i32 bestChordTone = targetMidi;
            i32 bestDist = 999;
            for (i32 cn : curChord->midiNotes) {
                // Try the chord tone in the melody octave range.
                for (int oct = -1; oct <= 2; ++oct) {
                    i32 candidate = cn + oct * 12;
                    i32 dist = std::abs(targetMidi - candidate);
                    if (dist < bestDist) { bestDist = dist; bestChordTone = candidate; }
                }
            }
            targetMidi = bestChordTone;
        }

        // Snap to scale.
        targetMidi = SnapMidiToScale(targetMidi, root, scale);

        NoteEvent ev;
        ev.startBeat = beat;
        ev.duration  = noteLen * 0.9f; // slight gap for articulation.
        ev.midiNote  = targetMidi;

        // Velocity from brightness.
        ev.velocity  = 0.5f + pitchContour[i] * 0.4f;

        melody.push_back(ev);
        prevMidi = targetMidi;
    }

    return melody;
}

// ── Build bass line ─────────────────────────────────────────────────────────

std::vector<NoteEvent> ImageComposer::BuildBass(
    const std::vector<ChordEvent>& chords, f32 totalBeats, CompStyle style)
{
    std::vector<NoteEvent> bass;
    if (chords.empty()) return bass;

    for (auto& chord : chords) {
        if (chord.midiNotes.empty()) continue;

        // Root note in bass register (2 octaves below chord root).
        i32 bassRoot = chord.midiNotes[0] - 24;
        // Clamp to reasonable bass range.
        while (bassRoot < 28) bassRoot += 12;
        while (bassRoot > 55) bassRoot -= 12;

        i32 fifth = bassRoot + 7;

        switch (style) {
        case CompStyle::Ethereal:
        case CompStyle::Cinematic: {
            // Sustained bass drone — one note per chord.
            NoteEvent ev;
            ev.startBeat = chord.startBeat;
            ev.duration  = chord.duration * 0.95f;
            ev.midiNote  = bassRoot;
            ev.velocity  = chord.velocity * 0.7f;
            bass.push_back(ev);
            break;
        }
        case CompStyle::Harmonic: {
            // Walking bass: root – 3rd – 5th – approach.
            f32 stepLen = chord.duration / 4.0f;
            i32 third = bassRoot + (chord.midiNotes.size() >= 2 ?
                (chord.midiNotes[1] - chord.midiNotes[0]) : 4);
            // Normalize third to bass octave.
            while (third > bassRoot + 12) third -= 12;
            while (third < bassRoot) third += 12;

            i32 walk[] = {bassRoot, third, fifth, bassRoot + 12};
            for (int j = 0; j < 4; ++j) {
                NoteEvent ev;
                ev.startBeat = chord.startBeat + j * stepLen;
                ev.duration  = stepLen * 0.85f;
                ev.midiNote  = walk[j];
                ev.velocity  = chord.velocity * (j == 0 ? 0.8f : 0.6f);
                bass.push_back(ev);
            }
            break;
        }
        case CompStyle::Rhythmic: {
            // Synth bass: 8th note octave pattern (root, root+12, root, root+12...).
            f32 stepLen = 0.5f;
            u32 steps = static_cast<u32>(chord.duration / stepLen);
            for (u32 j = 0; j < steps; ++j) {
                NoteEvent ev;
                ev.startBeat = chord.startBeat + j * stepLen;
                ev.duration  = stepLen * 0.7f;
                ev.midiNote  = (j % 2 == 0) ? bassRoot : bassRoot + 12;
                ev.velocity  = (j % 2 == 0) ? chord.velocity * 0.85f : chord.velocity * 0.55f;
                bass.push_back(ev);
            }
            break;
        }
        case CompStyle::Melodic:
        default: {
            // Root on beat 1, fifth on beat 3.
            f32 half = chord.duration / 2.0f;
            {
                NoteEvent ev;
                ev.startBeat = chord.startBeat;
                ev.duration  = half * 0.9f;
                ev.midiNote  = bassRoot;
                ev.velocity  = chord.velocity * 0.75f;
                bass.push_back(ev);
            }
            {
                NoteEvent ev;
                ev.startBeat = chord.startBeat + half;
                ev.duration  = half * 0.9f;
                ev.midiNote  = fifth;
                ev.velocity  = chord.velocity * 0.6f;
                bass.push_back(ev);
            }
            break;
        }
        }
    }

    return bass;
}

// ── Build drum pattern ──────────────────────────────────────────────────────

std::vector<DrumHit> ImageComposer::BuildDrums(
    const ImageAnalysis& a, f32 totalBeats, CompStyle style)
{
    std::vector<DrumHit> drums;

    // Base density from image.
    f32 density = a.edges.density * 0.5f + a.texture.roughness * 0.5f;

    switch (style) {
    case CompStyle::Ethereal: {
        // Very sparse — occasional soft hi-hat.
        for (f32 b = 0; b < totalBeats; b += 2.0f) {
            if (density > 0.3f) {
                drums.push_back({b, DrumType::HiHat, 0.2f});
            }
        }
        break;
    }
    case CompStyle::Cinematic: {
        // Occasional deep kick on beat 1 of each 4-bar section.
        for (f32 b = 0; b < totalBeats; b += 16.0f) {
            drums.push_back({b, DrumType::Kick, 0.7f});
        }
        // Sparse snare rolls at climax points.
        if (density > 0.5f) {
            f32 mid = totalBeats * 0.5f;
            drums.push_back({mid, DrumType::Snare, 0.5f});
        }
        break;
    }
    case CompStyle::Rhythmic: {
        // Full kit: kick on 1,3; snare on 2,4; hi-hat on 8ths or 16ths.
        for (f32 b = 0; b < totalBeats; b += 4.0f) {
            drums.push_back({b,        DrumType::Kick,  0.8f});
            drums.push_back({b + 2.0f, DrumType::Kick,  0.6f});
            drums.push_back({b + 1.0f, DrumType::Snare, 0.7f});
            drums.push_back({b + 3.0f, DrumType::Snare, 0.65f});

            // If high density, add ghost kicks.
            if (density > 0.5f) {
                drums.push_back({b + 2.5f, DrumType::Kick, 0.35f});
            }
        }

        // Hi-hat pattern.
        f32 hatStep = (density > 0.6f) ? 0.25f : 0.5f;
        for (f32 b = 0; b < totalBeats; b += hatStep) {
            bool isOffbeat = (std::fmod(b, 1.0f) > 0.01f);
            f32 vel = isOffbeat ? 0.3f : 0.5f;
            // Occasional open hat on "and" of 2 and 4.
            bool openHat = (std::fmod(b - 1.5f, 4.0f) < 0.01f || std::fmod(b - 3.5f, 4.0f) < 0.01f);
            drums.push_back({b, openHat ? DrumType::OpenHat : DrumType::HiHat, vel});
        }
        break;
    }
    case CompStyle::Harmonic: {
        // Moderate: kick on 1, snare on 3, hi-hat on 8ths.
        for (f32 b = 0; b < totalBeats; b += 4.0f) {
            drums.push_back({b,        DrumType::Kick,  0.6f});
            drums.push_back({b + 2.0f, DrumType::Snare, 0.45f});
        }
        for (f32 b = 0; b < totalBeats; b += 0.5f) {
            f32 vel = (std::fmod(b, 1.0f) < 0.01f) ? 0.35f : 0.2f;
            drums.push_back({b, DrumType::HiHat, vel});
        }
        break;
    }
    case CompStyle::Melodic:
    default: {
        // Light: just hi-hat and gentle kick.
        for (f32 b = 0; b < totalBeats; b += 4.0f) {
            drums.push_back({b, DrumType::Kick, 0.4f});
        }
        for (f32 b = 0; b < totalBeats; b += 1.0f) {
            drums.push_back({b, DrumType::HiHat, 0.25f});
        }
        break;
    }
    }

    // Sort by beat time.
    std::sort(drums.begin(), drums.end(),
        [](const DrumHit& a, const DrumHit& b) { return a.beat < b.beat; });

    return drums;
}

// ═══════════════════════════════════════════════════════════════════════════
// MusicRenderer
// ═══════════════════════════════════════════════════════════════════════════

void MusicRenderer::Init(u32 sampleRate) {
    sampleRate_ = sampleRate;
    Reset();
}

void MusicRenderer::SetComposition(Composition comp) {
    comp_ = std::move(comp);
    Reset();
}

void MusicRenderer::Reset() {
    totalSamples_ = 0;
    prevBeat_ = -1.0f;
    nextMelody_ = nextChord_ = nextBass_ = nextDrum_ = 0;

    for (auto& v : melodyVoices_) v = {};
    for (auto& v : chordVoices_)  v = {};
    for (auto& v : bassVoices_)   v = {};
    for (auto& v : drumVoices_)   v = {};
    masterFilterL_ = masterFilterR_ = 0;
}

f32 MusicRenderer::BeatAt(u64 sample) const {
    f32 effectiveTempo = comp_.tempo * tempoScale_;
    return static_cast<f32>(static_cast<f64>(sample) * effectiveTempo / (60.0 * sampleRate_));
}

f32 MusicRenderer::Noise() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<f32>(rng_ & 0xFFFF) / 32768.0f - 1.0f;
}

// ── Event triggering ────────────────────────────────────────────────────────

void MusicRenderer::TriggerMelody(const NoteEvent& n) {
    // Find the least-active voice.
    Voice* best = &melodyVoices_[0];
    for (auto& v : melodyVoices_) {
        if (!v.active) { best = &v; break; }
        if (v.envLevel < best->envLevel) best = &v;
    }
    best->active     = true;
    best->freq       = MidiToFreq(static_cast<f32>(n.midiNote));
    best->amp        = n.velocity;
    best->phase      = 0;
    best->phase2     = 0;
    best->envLevel   = 0;
    best->envStage   = Attack;
    best->attackRate  = 1.0f / (comp_.melodyAttack * sampleRate_);
    best->releaseRate = 1.0f / (0.3f * sampleRate_);
    best->samplesLeft = static_cast<u32>(n.duration * 60.0f / (comp_.tempo * tempoScale_) * sampleRate_);
    best->pan         = 0.0f; // melody centered.
    best->filterState = 0;
}

void MusicRenderer::TriggerChord(const ChordEvent& c) {
    // Assign one voice per chord note, releasing old ones.
    // First, set all active chord voices to release.
    for (auto& v : chordVoices_) {
        if (v.active && v.envStage != Release) v.envStage = Release;
    }

    u32 voiceIdx = 0;
    for (i32 midiNote : c.midiNotes) {
        if (voiceIdx >= kChordVoices) break;

        // Find a free or releasing voice.
        Voice* best = nullptr;
        for (auto& v : chordVoices_) {
            if (!v.active) { best = &v; break; }
        }
        if (!best) {
            // Steal the one with lowest envelope.
            best = &chordVoices_[0];
            for (auto& v : chordVoices_) {
                if (v.envLevel < best->envLevel) best = &v;
            }
        }

        best->active      = true;
        best->freq        = MidiToFreq(static_cast<f32>(midiNote));
        best->amp         = c.velocity;
        best->phase       = 0;
        best->phase2      = 0.3; // offset for detuned 2nd osc.
        best->envLevel    = 0;
        best->envStage    = Attack;
        best->attackRate  = 1.0f / (comp_.padAttack * sampleRate_);
        best->releaseRate = 1.0f / (1.0f * sampleRate_);
        best->samplesLeft = static_cast<u32>(c.duration * 60.0f / (comp_.tempo * tempoScale_) * sampleRate_);

        // Spread chord voices in stereo.
        f32 spread = static_cast<f32>(voiceIdx) / static_cast<f32>(c.midiNotes.size());
        best->pan = (spread - 0.5f) * 2.0f * stereoWidth_;
        best->filterState = 0;
        ++voiceIdx;
    }
}

void MusicRenderer::TriggerBass(const NoteEvent& n) {
    Voice* best = &bassVoices_[0];
    for (auto& v : bassVoices_) {
        if (!v.active) { best = &v; break; }
        if (v.envLevel < best->envLevel) best = &v;
    }
    best->active      = true;
    best->freq        = MidiToFreq(static_cast<f32>(n.midiNote));
    best->amp         = n.velocity;
    best->phase       = 0;
    best->envLevel    = 0;
    best->envStage    = Attack;
    best->attackRate  = 1.0f / (0.01f * sampleRate_);
    best->releaseRate = 1.0f / (0.15f * sampleRate_);
    best->samplesLeft = static_cast<u32>(n.duration * 60.0f / (comp_.tempo * tempoScale_) * sampleRate_);
    best->pan         = 0; // bass centered.
    best->filterState = 0;
}

void MusicRenderer::TriggerDrum(const DrumHit& h) {
    // Find a free drum voice.
    DrumVoice* best = nullptr;
    for (auto& v : drumVoices_) {
        if (!v.active) { best = &v; break; }
    }
    if (!best) {
        // Steal oldest (lowest env).
        best = &drumVoices_[0];
        for (auto& v : drumVoices_) {
            if (v.env < best->env) best = &v;
        }
    }
    best->active = true;
    best->type   = h.type;
    best->env    = 1.0f;
    best->phase  = 0;
    best->amp    = h.velocity;
    best->filterState = 0;
}

// ── Voice update ────────────────────────────────────────────────────────────

void MusicRenderer::UpdateVoice(Voice& v) {
    if (!v.active) return;

    switch (v.envStage) {
    case Attack:
        v.envLevel += v.attackRate;
        if (v.envLevel >= 1.0f) { v.envLevel = 1.0f; v.envStage = Sustain; }
        break;
    case Sustain:
        if (v.samplesLeft == 0) v.envStage = Release;
        break;
    case Release:
        v.envLevel -= v.releaseRate * v.envLevel;
        if (v.envLevel < 0.001f) { v.envLevel = 0; v.active = false; }
        break;
    default: break;
    }

    if (v.samplesLeft > 0) --v.samplesLeft;
}

// ── Voice rendering ─────────────────────────────────────────────────────────

f32 MusicRenderer::RenderMelodyVoice(Voice& v) {
    if (!v.active) return 0;

    // Additive saw-like: fundamental + 2 harmonics.
    f32 p = static_cast<f32>(v.phase);
    f32 sample = std::sin(kTwoPI * p)
               + 0.4f * std::sin(kTwoPI * 2.0f * p)
               + 0.15f * std::sin(kTwoPI * 3.0f * p)
               + 0.06f * std::sin(kTwoPI * 4.0f * p);
    sample *= 0.5f; // normalize.

    sample *= v.envLevel * v.amp;

    // One-pole lowpass for brightness.
    f32 cutoff = 0.1f + brightness_ * 0.7f;
    v.filterState += cutoff * (sample - v.filterState);
    sample = v.filterState;

    v.phase += static_cast<f64>(v.freq) / sampleRate_;
    if (v.phase >= 1.0) v.phase -= 1.0;

    UpdateVoice(v);
    return sample;
}

void MusicRenderer::RenderChordVoice(Voice& v, f32& left, f32& right) {
    if (!v.active) return;

    // Detuned pair of oscillators for warmth.
    f32 p1 = static_cast<f32>(v.phase);
    f32 p2 = static_cast<f32>(v.phase2);

    // Soft saw-like via additive.
    f32 s1 = std::sin(kTwoPI * p1)
           + 0.35f * std::sin(kTwoPI * 2.0f * p1)
           + 0.1f * std::sin(kTwoPI * 3.0f * p1);
    f32 s2 = std::sin(kTwoPI * p2)
           + 0.35f * std::sin(kTwoPI * 2.0f * p2)
           + 0.1f * std::sin(kTwoPI * 3.0f * p2);

    f32 sample = (s1 + s2) * 0.25f * v.envLevel * v.amp;

    // Lowpass for warmth.
    f32 cutoff = 0.05f + brightness_ * 0.4f;
    v.filterState += cutoff * (sample - v.filterState);
    sample = v.filterState;

    // Pan.
    f32 panL = std::cos((v.pan + 1.0f) * kHalfPI * 0.5f);
    f32 panR = std::sin((v.pan + 1.0f) * kHalfPI * 0.5f);
    left  += sample * panL;
    right += sample * panR;

    // Advance with slight detune.
    f32 detune = 1.003f; // ~5 cents.
    v.phase  += static_cast<f64>(v.freq) / sampleRate_;
    v.phase2 += static_cast<f64>(v.freq * detune) / sampleRate_;
    if (v.phase  >= 1.0) v.phase  -= 1.0;
    if (v.phase2 >= 1.0) v.phase2 -= 1.0;

    UpdateVoice(v);
}

f32 MusicRenderer::RenderBassVoice(Voice& v) {
    if (!v.active) return 0;

    // Sine + subtle overtone for warmth.
    f32 p = static_cast<f32>(v.phase);
    f32 sample = std::sin(kTwoPI * p)
               + 0.2f * std::sin(kTwoPI * 2.0f * p)
               + 0.05f * std::sin(kTwoPI * 3.0f * p);
    sample *= 0.6f;

    sample *= v.envLevel * v.amp;

    // Heavy lowpass.
    f32 cutoff = 0.08f + brightness_ * 0.15f;
    v.filterState += cutoff * (sample - v.filterState);
    sample = v.filterState;

    v.phase += static_cast<f64>(v.freq) / sampleRate_;
    if (v.phase >= 1.0) v.phase -= 1.0;

    UpdateVoice(v);
    return sample;
}

f32 MusicRenderer::RenderDrumVoice(DrumVoice& v) {
    if (!v.active) return 0;

    f32 sample = 0;
    f32 decayRate;

    switch (v.type) {
    case DrumType::Kick: {
        // Pitch-dropping sine.
        f32 freq = 50.0f + 100.0f * v.env * v.env;
        sample = std::sin(kTwoPI * static_cast<f32>(v.phase)) * v.env;
        v.phase += static_cast<f64>(freq) / sampleRate_;
        decayRate = 1.0f - 5.0f / sampleRate_;   // ~200ms decay.
        break;
    }
    case DrumType::Snare: {
        // Body (sine) + noise.
        f32 body = std::sin(kTwoPI * static_cast<f32>(v.phase)) * v.env * 0.5f;
        v.phase += 180.0 / sampleRate_;
        f32 noise = Noise() * v.env * 0.5f;
        // Bandpass the noise via simple feedback.
        v.filterState += 0.3f * (noise - v.filterState);
        noise = noise - v.filterState;
        sample = body + noise;
        decayRate = 1.0f - 7.0f / sampleRate_;   // ~140ms decay.
        break;
    }
    case DrumType::HiHat: {
        f32 noise = Noise();
        // High-pass via one-pole.
        v.filterState += 0.15f * (noise - v.filterState);
        sample = (noise - v.filterState) * v.env * 0.4f;
        decayRate = 1.0f - 30.0f / sampleRate_;  // ~30ms decay (closed hat).
        break;
    }
    case DrumType::OpenHat: {
        f32 noise = Noise();
        v.filterState += 0.15f * (noise - v.filterState);
        sample = (noise - v.filterState) * v.env * 0.35f;
        decayRate = 1.0f - 5.0f / sampleRate_;   // ~200ms decay (open hat).
        break;
    }
    }

    sample *= v.amp;
    v.env *= decayRate;
    if (v.env < 0.001f) { v.active = false; v.env = 0; }

    return sample;
}

// ── Main render loop ────────────────────────────────────────────────────────

void MusicRenderer::Process(SampleBuffer* output, u32 bufferSize) {
    if (!output || comp_.totalBeats <= 0) return;

    for (u32 f = 0; f < bufferSize; ++f) {
        f32 beat = BeatAt(totalSamples_);
        f32 loopBeat = std::fmod(beat, comp_.totalBeats);

        // Detect loop restart.
        if (loopBeat < prevBeat_ - 0.5f) {
            nextMelody_ = 0;
            nextChord_  = 0;
            nextBass_   = 0;
            nextDrum_   = 0;
        }

        // ── Trigger events ──────────────────────────────────────────────
        while (nextMelody_ < comp_.melody.size() &&
               comp_.melody[nextMelody_].startBeat <= loopBeat) {
            TriggerMelody(comp_.melody[nextMelody_]);
            ++nextMelody_;
        }
        while (nextChord_ < comp_.chords.size() &&
               comp_.chords[nextChord_].startBeat <= loopBeat) {
            TriggerChord(comp_.chords[nextChord_]);
            ++nextChord_;
        }
        while (nextBass_ < comp_.bass.size() &&
               comp_.bass[nextBass_].startBeat <= loopBeat) {
            TriggerBass(comp_.bass[nextBass_]);
            ++nextBass_;
        }
        while (nextDrum_ < comp_.drums.size() &&
               comp_.drums[nextDrum_].beat <= loopBeat) {
            TriggerDrum(comp_.drums[nextDrum_]);
            ++nextDrum_;
        }

        // ── Render all layers ───────────────────────────────────────────
        f32 melSample = 0;
        for (auto& v : melodyVoices_) melSample += RenderMelodyVoice(v);

        f32 chordL = 0, chordR = 0;
        for (auto& v : chordVoices_) RenderChordVoice(v, chordL, chordR);

        f32 bassSample = 0;
        for (auto& v : bassVoices_) bassSample += RenderBassVoice(v);

        f32 drumSample = 0;
        for (auto& v : drumVoices_) drumSample += RenderDrumVoice(v);

        // ── Mix ─────────────────────────────────────────────────────────
        f32 eg = energy_;
        f32 left  = melSample * comp_.melodyGain
                  + chordL    * comp_.chordGain
                  + bassSample * comp_.bassGain
                  + drumSample * comp_.drumGain;
        f32 right = melSample * comp_.melodyGain
                  + chordR    * comp_.chordGain
                  + bassSample * comp_.bassGain
                  + drumSample * comp_.drumGain;

        // Apply energy scaling.
        left  *= (0.3f + eg * 0.7f);
        right *= (0.3f + eg * 0.7f);

        // Master lowpass for global warmth/brightness.
        f32 masterCutoff = 0.3f + brightness_ * 0.7f;
        masterFilterL_ += masterCutoff * (left  - masterFilterL_);
        masterFilterR_ += masterCutoff * (right - masterFilterR_);

        // Soft-clip (tanh).
        left  = std::tanh(masterFilterL_ * 1.5f);
        right = std::tanh(masterFilterR_ * 1.5f);

        output->SetSample(0, f, left);
        if (output->Channels() > 1) output->SetSample(1, f, right);

        prevBeat_ = loopBeat;
        ++totalSamples_;
    }
}

} // namespace sky
