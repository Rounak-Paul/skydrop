/// @file image_composer.cpp
/// @brief Image-to-music composition with musical structure, motifs, and dynamics.
///
/// The image drives every musical element:
///   - Structure: image divided into vertical regions; color similarity → section repeats
///   - Motifs: melodic phrases extracted from image regions; repeated with variation
///   - Builds/drops: brightness gradients → crescendo/decrescendo
///   - Chord progressions: repeated for similar sections
///   - Drum density: section-aware (sparse intro, full chorus, fills at transitions)
///   - Bass style: section-aware (sustained in verse, punchy in chorus)
///   - Smoothness: portamento derived from image texture regularity
///   - Dynamics: section energy envelopes, not flat brightness scanning

#include "skydrop/synth/image_composer.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace sky {

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
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

static u32 NextRNG(u32& rng) {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static f32 RandF(u32& rng) {
    return static_cast<f32>(NextRNG(rng) & 0xFFFF) / 65535.0f;
}

static std::array<f32, 3> ToHSB(f32 r, f32 g, f32 b) {
    f32 mx = std::max({r, g, b});
    f32 mn = std::min({r, g, b});
    f32 d  = mx - mn;
    f32 h = 0, s = 0, v = mx;
    if (mx > 1e-6f) s = d / mx;
    if (d > 1e-6f) {
        if      (mx == r) h = 60.0f * std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
        else              h = 60.0f * ((r - g) / d + 4.0f);
    }
    if (h < 0) h += 360.0f;
    return {h, s, v};
}

static f32 ColorDist(const std::array<f32,4>& a, const std::array<f32,4>& b) {
    f32 dr = a[0]-b[0], dg = a[1]-b[1], db = a[2]-b[2];
    return std::sqrt(dr*dr + dg*dg + db*db);
}

static void Smooth(std::vector<f32>& v, u32 window) {
    if (v.size() < 2 || window < 2) return;
    std::vector<f32> tmp(v.size());
    i32 half = static_cast<i32>(window) / 2;
    for (i32 i = 0; i < static_cast<i32>(v.size()); ++i) {
        f32 sum = 0; u32 cnt = 0;
        for (i32 j = i - half; j <= i + half; ++j) {
            if (j >= 0 && j < static_cast<i32>(v.size())) { sum += v[j]; ++cnt; }
        }
        tmp[i] = sum / std::max(cnt, 1u);
    }
    v = std::move(tmp);
}

static f32 SampleProfile(const std::vector<f32>& prof, f32 t) {
    if (prof.empty()) return 0;
    t = std::clamp(t, 0.0f, 1.0f);
    f32 idx = t * static_cast<f32>(prof.size() - 1);
    u32 lo = static_cast<u32>(std::floor(idx));
    u32 hi = std::min(lo + 1, static_cast<u32>(prof.size() - 1));
    f32 frac = idx - static_cast<f32>(lo);
    return prof[lo] * (1.0f - frac) + prof[hi] * frac;
}

static f32 QuantizeDuration(f32 dur) {
    static const f32 grid[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    f32 best = grid[0];
    f32 bestDist = std::abs(dur - grid[0]);
    for (f32 g : grid) {
        f32 d = std::abs(dur - g);
        if (d < bestDist) { bestDist = d; best = g; }
    }
    return best;
}

/// Find which section contains a given beat.
static const Section* SectionAt(const std::vector<Section>& sections, f32 beat) {
    for (auto& s : sections)
        if (beat >= s.startBeat && beat < s.endBeat) return &s;
    return sections.empty() ? nullptr : &sections.back();
}


// ═══════════════════════════════════════════════════════════════════════════
// ImageComposer::Compose
// ═══════════════════════════════════════════════════════════════════════════

Composition ImageComposer::Compose(const ImageData& image,
                                   const ImageAnalysis& analysis,
                                   CompStyle style)
{
    Composition c;

    c.tempo    = DeriveTempo(analysis, style);
    c.rootNote = DeriveRootNote(analysis);
    c.scale    = DeriveScale(analysis);

    // Longer loops for proper structure: 16-32 bars (64-128 beats).
    f32 aspect = static_cast<f32>(image.Width()) / std::max(1u, image.Height());
    f32 baseBars;
    switch (style) {
    case CompStyle::Ethereal:  baseBars = 20.0f + aspect * 4.0f; break;
    case CompStyle::Cinematic: baseBars = 24.0f + aspect * 4.0f; break;
    case CompStyle::Rhythmic:  baseBars = 16.0f + aspect * 2.0f; break;
    case CompStyle::Melodic:   baseBars = 16.0f + aspect * 4.0f; break;
    case CompStyle::Harmonic:  baseBars = 16.0f + aspect * 4.0f; break;
    default:                   baseBars = 16.0f + aspect * 4.0f; break;
    }
    // Quantize to multiple of 4 bars for clean structure.
    u32 bars = (static_cast<u32>(std::clamp(baseBars, 16.0f, 32.0f)) / 4) * 4;
    c.totalBeats = static_cast<f32>(bars * 4);

    // Image-derived timbre.
    c.harmonicProfile = BuildHarmonicProfile(analysis);
    c.detuneAmount    = 0.001f + analysis.color.complexity * 0.006f;
    c.noiseBlend      = analysis.texture.roughness * 0.15f;
    c.filterSweep     = analysis.texture.regularity * 0.4f;
    c.portamentoRate  = analysis.texture.regularity * 0.6f
                      + (1.0f - analysis.texture.roughness) * 0.3f;
    // Lead chordiness: saturated, complex images → more chord leads.
    c.leadChordiness  = analysis.color.saturation * 0.5f
                      + analysis.color.complexity * 0.3f
                      + (1.0f - analysis.texture.roughness) * 0.2f;
    c.leadChordiness  = std::clamp(c.leadChordiness, 0.0f, 1.0f);
    // Melody release: smoother images get longer sustain.
    c.melodyRelease   = 0.2f + (1.0f - analysis.texture.roughness) * 0.4f
                      + analysis.texture.regularity * 0.2f;

    // Brightness / warmth from image.
    c.brightness = analysis.color.brightness;
    c.warmth     = (analysis.color.warmth + 1.0f) * 0.5f;

    // ── Build structural plan from image ────────────────────────────
    c.sections = BuildStructure(image, analysis, c.totalBeats, style);

    // ── Build dynamic map from sections (not flat brightness scan) ──
    c.dynamicMap = BuildDynamicMap(image, c.sections, c.totalBeats);

    // ── Build musical layers (section-aware) ────────────────────────
    c.chords = BuildChords(image, analysis, c.rootNote, *c.scale, c.totalBeats, style);
    c.melody = BuildMelody(image, analysis, c.chords, c.rootNote, *c.scale, c.totalBeats, style);
    c.bass   = BuildBass(image, analysis, c.chords, c.rootNote, *c.scale, c.totalBeats, style);
    c.drums  = BuildDrums(image, analysis, c.totalBeats, style);

    // Gain settings.
    f32 imgEnergy = analysis.edges.density * 0.4f + analysis.texture.roughness * 0.3f
                  + analysis.color.brightness * 0.3f;

    switch (style) {
    case CompStyle::Ethereal:
        c.melodyGain = 0.18f; c.chordGain = 0.25f; c.bassGain = 0.18f; c.drumGain = 0.04f;
        c.padAttack = 1.5f; c.melodyAttack = 0.3f; break;
    case CompStyle::Harmonic:
        c.melodyGain = 0.28f; c.chordGain = 0.22f; c.bassGain = 0.20f; c.drumGain = 0.10f;
        c.padAttack = 0.4f; c.melodyAttack = 0.02f; break;
    case CompStyle::Rhythmic:
        c.melodyGain = 0.22f; c.chordGain = 0.15f; c.bassGain = 0.28f; c.drumGain = 0.20f;
        c.padAttack = 0.05f; c.melodyAttack = 0.005f; break;
    case CompStyle::Melodic:
        c.melodyGain = 0.32f; c.chordGain = 0.20f; c.bassGain = 0.18f; c.drumGain = 0.08f;
        c.padAttack = 0.6f; c.melodyAttack = 0.01f; break;
    case CompStyle::Cinematic:
        c.melodyGain = 0.15f; c.chordGain = 0.30f; c.bassGain = 0.22f; c.drumGain = 0.05f;
        c.padAttack = 2.0f; c.melodyAttack = 0.5f; break;
    default: break;
    }

    c.drumGain *= (0.5f + imgEnergy);
    c.bassGain *= (0.7f + imgEnergy * 0.6f);

    return c;
}


// ═══════════════════════════════════════════════════════════════════════════
// Derive tempo, key, scale
// ═══════════════════════════════════════════════════════════════════════════

f32 ImageComposer::DeriveTempo(const ImageAnalysis& a, CompStyle style) {
    f32 energy = a.edges.density * 0.5f + a.texture.roughness * 0.3f
               + a.color.brightness * 0.2f;
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

i32 ImageComposer::DeriveRootNote(const ImageAnalysis& a) {
    static const i32 hueToPC[] = {0,7,2,9,4,11,6,1,8,3,10,5};
    i32 segment = static_cast<i32>(a.color.dominantHue / 30.0f) % 12;
    return 48 + hueToPC[segment];
}

const std::vector<i32>* ImageComposer::DeriveScale(const ImageAnalysis& a) {
    bool warm   = a.color.warmth > 0.0f;
    bool bright = a.color.brightness > 0.6f;
    f32  comp   = a.color.complexity;
    f32  sat    = a.color.saturation;

    if (comp < 0.25f) {
        // Simple images: pentatonic scales (hard to hit wrong notes).
        return warm ? &scales::Pentatonic : &scales::MinorPenta;
    } else if (comp > 0.7f && sat > 0.5f) {
        // Complex, saturated images: modal scales.
        return warm ? &scales::Mixolydian : &scales::Dorian;
    } else if (!warm && !bright) {
        // Dark, cool images: full minor.
        return &scales::Minor;
    } else if (warm && bright) {
        // Warm, bright images: major.
        return &scales::Major;
    } else if (!warm && comp > 0.4f) {
        // Cool complex: blues.
        return &scales::Blues;
    } else {
        return warm ? &scales::Major : &scales::Minor;
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// BuildStructure — image regions → song sections
// ═══════════════════════════════════════════════════════════════════════════

std::vector<Section> ImageComposer::BuildStructure(
    const ImageData& img, const ImageAnalysis& a, f32 totalBeats, CompStyle style)
{
    // Divide image into 8 vertical regions.
    constexpr u32 kNumRegions = 8;
    u32 totalBars = static_cast<u32>(totalBeats / 4.0f);
    u32 barsPerRegion = std::max(1u, totalBars / kNumRegions);

    // ── 1. Sample average color + brightness per region ─────────────
    struct RegionInfo {
        std::array<f32, 3> hsb;  // average HSB
        f32 brightness;
        f32 edgeDensity;
    };
    std::vector<RegionInfo> regions(kNumRegions);

    for (u32 i = 0; i < kNumRegions; ++i) {
        u32 x = i * img.Width() / kNumRegions;
        u32 w = std::max(1u, img.Width() / kNumRegions);
        if (x + w > img.Width()) w = img.Width() - x;

        auto col = img.AverageColor(x, 0, w, img.Height());
        regions[i].hsb = ToHSB(col[0], col[1], col[2]);
        regions[i].brightness = img.AverageBrightness(x, 0, w, img.Height());
        f32 t = static_cast<f32>(i) / kNumRegions;
        regions[i].edgeDensity = SampleProfile(a.edges.verticalProfile, t);
    }

    // ── 2. Compute color similarity between regions ─────────────────
    // similarity[i][j] = 1.0 if identical, 0 if maximally different
    auto colorSim = [&](u32 i, u32 j) -> f32 {
        f32 dh = std::abs(regions[i].hsb[0] - regions[j].hsb[0]);
        if (dh > 180) dh = 360 - dh;
        dh /= 180.0f;
        f32 ds = std::abs(regions[i].hsb[1] - regions[j].hsb[1]);
        f32 db = std::abs(regions[i].brightness - regions[j].brightness);
        return 1.0f - std::clamp((dh * 0.5f + ds * 0.3f + db * 0.2f), 0.0f, 1.0f);
    };

    // ── 3. Find brightness gradient (for builds/drops) ──────────────
    std::vector<f32> briGrad(kNumRegions, 0.0f);
    for (u32 i = 1; i < kNumRegions; ++i)
        briGrad[i] = regions[i].brightness - regions[i-1].brightness;

    // ── 4. Find the brightest region → Chorus candidate ─────────────
    u32 chorusRegion = 0;
    f32 maxBri = 0;
    for (u32 i = 1; i < kNumRegions - 1; ++i) {
        if (regions[i].brightness > maxBri) {
            maxBri = regions[i].brightness;
            chorusRegion = i;
        }
    }

    // ── 5. Assign section types ─────────────────────────────────────
    std::vector<SectionType> typeMap(kNumRegions);
    std::vector<u32> regionGroup(kNumRegions);

    // First pass: assign primary types.
    typeMap[0] = SectionType::Intro;
    typeMap[kNumRegions - 1] = SectionType::Outro;
    typeMap[chorusRegion] = SectionType::Chorus;

    for (u32 i = 1; i < kNumRegions - 1; ++i) {
        if (i == chorusRegion) continue;

        // Region similar to chorus → also Chorus.
        if (colorSim(i, chorusRegion) > 0.75f) {
            typeMap[i] = SectionType::Chorus;
        }
        // Big brightness jump → region before it is a Build/PreChorus.
        else if (i + 1 < kNumRegions && briGrad[i + 1] > 0.15f) {
            typeMap[i] = SectionType::PreChorus;
        }
        // Biggest brightness drop from previous → Drop.
        else if (briGrad[i] < -0.20f) {
            typeMap[i] = SectionType::Drop;
        }
        // Low brightness + low edge → Breakdown.
        else if (regions[i].brightness < 0.3f && regions[i].edgeDensity < 0.2f) {
            typeMap[i] = SectionType::Breakdown;
        }
        // Similar to region 0 or 1 → Verse.
        else if (colorSim(i, 0) > 0.6f || colorSim(i, 1) > 0.6f) {
            typeMap[i] = SectionType::Verse;
        }
        // Otherwise → Bridge.
        else {
            typeMap[i] = SectionType::Bridge;
        }
    }

    // Assign region groups (similar regions get same group for motif reuse).
    for (u32 i = 0; i < kNumRegions; ++i) regionGroup[i] = i;
    for (u32 i = 0; i < kNumRegions; ++i) {
        for (u32 j = 0; j < i; ++j) {
            if (typeMap[i] == typeMap[j] && colorSim(i, j) > 0.55f) {
                regionGroup[i] = regionGroup[j];
                break;
            }
        }
    }

    // ── 6. Build section objects ────────────────────────────────────
    std::vector<Section> sections;
    for (u32 i = 0; i < kNumRegions; ++i) {
        Section s;
        s.type          = typeMap[i];
        s.startBeat     = static_cast<f32>(i * barsPerRegion * 4);
        s.endBeat       = static_cast<f32>(std::min((i + 1) * barsPerRegion, totalBars) * 4);
        s.avgBrightness = regions[i].brightness;
        s.regionIdx     = regionGroup[i];

        // Energy: chorus/drop = high, intro/outro/breakdown = low.
        switch (s.type) {
        case SectionType::Chorus:    s.energy = 0.85f + regions[i].brightness * 0.15f; break;
        case SectionType::Drop:      s.energy = 0.9f; break;
        case SectionType::PreChorus: s.energy = 0.6f + regions[i].brightness * 0.2f; break;
        case SectionType::Verse:     s.energy = 0.45f + regions[i].brightness * 0.2f; break;
        case SectionType::Bridge:    s.energy = 0.35f + regions[i].brightness * 0.15f; break;
        case SectionType::Breakdown: s.energy = 0.15f; break;
        case SectionType::Intro:     s.energy = 0.2f + regions[i].brightness * 0.15f; break;
        case SectionType::Outro:     s.energy = 0.3f; break;
        }

        // Detect build (brightness rising toward next section).
        s.isBuild = (s.type == SectionType::PreChorus) ||
                    (i + 1 < kNumRegions && briGrad[i + 1] > 0.1f);

        // Detect drop (big brightness drop, like a beat drop).
        s.isDrop  = (s.type == SectionType::Drop) ||
                    (briGrad[i] < -0.2f && i > 0);

        // Style adjustments.
        if (style == CompStyle::Ethereal || style == CompStyle::Cinematic)
            s.energy *= 0.8f;
        if (style == CompStyle::Rhythmic)
            s.energy = std::max(s.energy, 0.3f);

        sections.push_back(s);
    }

    // Ensure last section ends exactly at totalBeats.
    if (!sections.empty())
        sections.back().endBeat = totalBeats;

    return sections;
}


// ═══════════════════════════════════════════════════════════════════════════
// ExtractMotif — short melodic phrase from an image region
// ═══════════════════════════════════════════════════════════════════════════

Motif ImageComposer::ExtractMotif(
    const ImageData& img, u32 x, u32 w,
    i32 root, const std::vector<i32>& scale, CompStyle style, u32& rng)
{
    Motif m;
    u32 numNotes;
    switch (style) {
    case CompStyle::Ethereal:  numNotes = 3 + (NextRNG(rng) % 3); break;  // 3-5
    case CompStyle::Cinematic: numNotes = 3 + (NextRNG(rng) % 3); break;
    case CompStyle::Melodic:   numNotes = 5 + (NextRNG(rng) % 4); break;  // 5-8
    case CompStyle::Harmonic:  numNotes = 4 + (NextRNG(rng) % 4); break;  // 4-7
    case CompStyle::Rhythmic:  numNotes = 6 + (NextRNG(rng) % 4); break;  // 6-9
    default:                   numNotes = 4 + (NextRNG(rng) % 4); break;
    }

    // Sample brightness centroids across the region.
    u32 vertSamples = 12;
    i32 prevInterval = 0;

    for (u32 n = 0; n < numNotes; ++n) {
        u32 sx = x + n * w / numNotes;
        u32 sw = std::max(1u, w / numNotes);
        if (sx + sw > img.Width()) sw = img.Width() - sx;

        // Brightness centroid → pitch.
        f32 weightedSum = 0, totalWeight = 0;
        for (u32 r = 0; r < vertSamples; ++r) {
            u32 y = r * img.Height() / vertSamples;
            u32 h = std::max(1u, img.Height() / vertSamples);
            f32 bri = img.AverageBrightness(sx, y, sw, h);
            f32 yNorm = 1.0f - static_cast<f32>(r) / static_cast<f32>(vertSamples);
            weightedSum += yNorm * bri;
            totalWeight += bri;
        }
        f32 centroid = (totalWeight > 1e-6f) ? (weightedSum / totalWeight) : 0.5f;

        // Convert to interval: centroid 0-1 → -7..+7 semitones (one 5th up/down).
        // Narrower range than before to keep melody in a comfortable register.
        i32 rawInterval = static_cast<i32>((centroid - 0.5f) * 14.0f);
        // Stepwise motion preference: limit jump from previous note (max 3 semitones step).
        if (rawInterval > prevInterval + 4) rawInterval = prevInterval + 4;
        if (rawInterval < prevInterval - 4) rawInterval = prevInterval - 4;
        // Snap to scale.
        i32 snapped = SnapMidiToScale(root + rawInterval, root, scale) - root;
        m.intervals.push_back(snapped);
        prevInterval = snapped;

        // Duration from color gradient.
        f32 dur = 1.0f; // default
        if (n > 0) {
            u32 prevX = x + (n - 1) * w / numNotes;
            auto c1 = img.AverageColor(prevX, 0, sw, img.Height());
            auto c2 = img.AverageColor(sx, 0, sw, img.Height());
            f32 grad = ColorDist(c1, c2);
            f32 gradNorm = std::clamp(grad * 5.0f, 0.0f, 1.0f);
            switch (style) {
            case CompStyle::Ethereal:  dur = 2.0f + (1.0f - gradNorm) * 2.0f; break;
            case CompStyle::Cinematic: dur = 2.0f + (1.0f - gradNorm) * 2.0f; break;
            case CompStyle::Rhythmic:  dur = 0.25f + (1.0f - gradNorm) * 0.75f; break;
            case CompStyle::Melodic:   dur = 0.5f + (1.0f - gradNorm) * 1.5f; break;
            default:                   dur = 0.5f + (1.0f - gradNorm) * 1.5f; break;
            }
        }
        m.durations.push_back(QuantizeDuration(dur));

        // Velocity from brightness.
        f32 bri = img.AverageBrightness(sx, 0, sw, img.Height());
        m.velocities.push_back(0.4f + bri * 0.5f);
    }

    return m;
}


// ═══════════════════════════════════════════════════════════════════════════
// BuildHarmonicProfile
// ═══════════════════════════════════════════════════════════════════════════

std::vector<f32> ImageComposer::BuildHarmonicProfile(const ImageAnalysis& a) {
    constexpr u32 kNumHarmonics = 8;
    std::vector<f32> profile(kNumHarmonics, 0.0f);

    if (a.frequencyBands.empty()) {
        // Natural 1/n harmonic rolloff (warm, not buzzy).
        for (u32 h = 0; h < kNumHarmonics; ++h)
            profile[h] = 1.0f / static_cast<f32>((h + 1) * (h + 1));  // 1/n^2 rolloff
        profile[0] = 1.0f;   // fundamental always dominant
        profile[1] = 0.5f;   // 2nd harmonic present
        profile[2] = 0.25f;  // 3rd at -12dB
        return profile;
    }

    u32 numBands = static_cast<u32>(a.frequencyBands.size());
    u32 groupSize = std::max(1u, numBands / kNumHarmonics);

    for (u32 h = 0; h < kNumHarmonics; ++h) {
        u32 startBand = h * groupSize;
        u32 endBand = std::min(startBand + groupSize, numBands);
        f32 sum = 0;
        for (u32 b = startBand; b < endBand; ++b)
            sum += a.frequencyBands[b].magnitude;
        profile[h] = sum / static_cast<f32>(endBand - startBand);
    }

    f32 maxVal = *std::max_element(profile.begin(), profile.end());
    if (maxVal > 1e-6f) {
        for (auto& v : profile) v /= maxVal;
    }
    profile[0] = std::max(profile[0], 0.6f);

    // Apply 1/n^1.5 spectral rolloff envelope to tame high harmonics.
    for (u32 h = 0; h < kNumHarmonics; ++h) {
        f32 rolloff = 1.0f / std::pow(static_cast<f32>(h + 1), 1.5f);
        profile[h] *= rolloff;
    }

    // Re-normalize.
    maxVal = *std::max_element(profile.begin(), profile.end());
    if (maxVal > 1e-6f)
        for (auto& v : profile) v /= maxVal;

    return profile;
}


// ═══════════════════════════════════════════════════════════════════════════
// BuildDynamicMap — section-aware dynamics with builds, drops, crescendos
// ═══════════════════════════════════════════════════════════════════════════

std::vector<f32> ImageComposer::BuildDynamicMap(
    const ImageData& img, const std::vector<Section>& sections, f32 totalBeats)
{
    u32 numBeats = static_cast<u32>(std::ceil(totalBeats));
    std::vector<f32> dmap(numBeats, 0.5f);

    for (u32 b = 0; b < numBeats; ++b) {
        f32 beat = static_cast<f32>(b);
        const Section* sec = SectionAt(sections, beat);
        if (!sec) continue;

        f32 secLen = sec->endBeat - sec->startBeat;
        f32 secPos = (beat - sec->startBeat) / std::max(1.0f, secLen);  // 0 to 1

        // Base intensity from section energy.
        f32 intensity = sec->energy;

        // Build: gradual crescendo through section.
        if (sec->isBuild) {
            intensity = sec->energy * (0.4f + 0.6f * secPos);
        }

        // Drop: silence at start, then slam in.
        if (sec->isDrop) {
            if (secPos < 0.1f)
                intensity = 0.05f;                 // near-silence
            else if (secPos < 0.15f)
                intensity = sec->energy * 1.2f;    // impact
            else
                intensity = sec->energy;
        }

        // Intro: fade in.
        if (sec->type == SectionType::Intro) {
            intensity *= (0.2f + 0.8f * secPos);
        }

        // Outro: fade out.
        if (sec->type == SectionType::Outro) {
            intensity *= (1.0f - secPos * 0.7f);
        }

        // Breakdown: low and steady.
        if (sec->type == SectionType::Breakdown) {
            intensity = 0.15f + 0.1f * secPos;
        }

        dmap[b] = std::clamp(intensity, 0.05f, 1.0f);
    }

    Smooth(dmap, 2);
    return dmap;
}


// ═══════════════════════════════════════════════════════════════════════════
// BuildChords — section-aware with repetition for same section types
// ═══════════════════════════════════════════════════════════════════════════

std::vector<ChordEvent> ImageComposer::BuildChords(
    const ImageData& img, const ImageAnalysis& a,
    i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style)
{
    std::vector<ChordEvent> chords;
    if (!img.IsLoaded()) return chords;

    // We need sections — use the composition's sections (passed via the Compose flow).
    // BuildChords is called after BuildStructure, and can access image directly.
    // We'll re-detect sections locally since they're not passed directly.
    auto sections = BuildStructure(img, a, totalBeats, style);

    // Beats per chord by style.
    f32 beatsPerChord;
    switch (style) {
    case CompStyle::Ethereal:  beatsPerChord = 4.0f; break;
    case CompStyle::Cinematic: beatsPerChord = 4.0f; break;
    case CompStyle::Harmonic:  beatsPerChord = 2.0f; break;
    case CompStyle::Rhythmic:  beatsPerChord = 2.0f; break;
    case CompStyle::Melodic:   beatsPerChord = 2.0f; break;
    default:                   beatsPerChord = 4.0f; break;
    }

    // Cache chord progressions per region index for repetition.
    std::unordered_map<u32, std::vector<ChordEvent>> regionChords;

    for (auto& sec : sections) {
        f32 secLen = sec.endBeat - sec.startBeat;
        u32 numChordsInSec = static_cast<u32>(secLen / beatsPerChord);
        if (numChordsInSec == 0) numChordsInSec = 1;

        // Check if we already generated chords for this region.
        auto it = regionChords.find(sec.regionIdx);
        if (it != regionChords.end() && !it->second.empty()) {
            // Reuse: copy chords with offset to this section's start beat.
            auto& cached = it->second;
            u32 chordsToUse = std::min(static_cast<u32>(cached.size()), numChordsInSec);
            for (u32 i = 0; i < numChordsInSec; ++i) {
                ChordEvent ev = cached[i % chordsToUse];
                ev.startBeat = sec.startBeat + static_cast<f32>(i) * beatsPerChord;
                ev.duration = beatsPerChord;
                chords.push_back(ev);
            }
            continue;
        }

        // Generate new chords for this section from image.
        std::vector<ChordEvent> secChords;

        f32 regionT = sec.startBeat / totalBeats;
        u32 imgX = static_cast<u32>(regionT * img.Width());
        u32 imgW = std::max(1u, static_cast<u32>(secLen / totalBeats * img.Width()));
        if (imgX + imgW > img.Width()) imgW = img.Width() - imgX;

        // ── Musical chord progressions (scale-degree based) ──────────
        // These stay within the scale so every chord is consonant with
        // the melody. The image hue/brightness selects which progression.
        // Scale degrees (0-indexed): I=0, II=1, III=2, IV=3, V=4, VI=5, VII=6
        // Each progression is listed as scale-degree indices.
        static const i32 kProgressions[6][4] = {
            {0, 5, 3, 4},  // I-VI-IV-V  (most common pop)
            {0, 3, 4, 0},  // I-IV-V-I   (classic)
            {5, 3, 0, 4},  // VI-IV-I-V  (melancholic)
            {0, 4, 5, 3},  // I-V-VI-IV  (axis progression)
            {0, 2, 3, 4},  // I-III-IV-V (ascending)
            {5, 6, 0, 4},  // VI-VII-I-V (dramatic)
        };

        // Choose progression from image hue/saturation of this section's region.
        auto avgColorSec = img.AverageColor(imgX, 0, imgW, img.Height());
        auto hsbSec = ToHSB(avgColorSec[0], avgColorSec[1], avgColorSec[2]);
        f32 secHue = hsbSec[0], secSat = hsbSec[1], secBri = hsbSec[2];

        // Section type biases the progression choice.
        u32 progIdx;
        switch (sec.type) {
        case SectionType::Chorus:    progIdx = 0; break;  // bright, uplifting
        case SectionType::Verse:     progIdx = 2; break;  // introspective
        case SectionType::PreChorus: progIdx = 3; break;  // building tension
        case SectionType::Bridge:    progIdx = 5; break;  // dramatic contrast
        case SectionType::Drop:      progIdx = 1; break;  // classic strong
        default:
            progIdx = static_cast<u32>((secHue / 60.0f)) % 6; break;
        }

        for (u32 i = 0; i < numChordsInSec; ++i) {
            // Get the scale degree from the progression (cycle if more chords than 4).
            i32 scaleDeg = kProgressions[progIdx][i % 4];
            // Map scale degree to actual MIDI note: root + scale[scaleDeg].
            i32 chordRoot = root + scale[std::min(scaleDeg, (i32)scale.size()-1)];
            // Place chord root in octave below melody center (C3-C4 range).
            while (chordRoot > 59) chordRoot -= 12;
            while (chordRoot < 40) chordRoot += 12;

            // Chord quality: use the intervals fitting the scale at this degree.
            // Build triad from scale: root, 3rd, 5th (all snapped to scale).
            i32 third = SnapMidiToScale(chordRoot + 4, root, scale);
            i32 fifth = SnapMidiToScale(chordRoot + 7, root, scale);
            // Keep third/fifth above root, below root+12.
            while (third  <= chordRoot) third  += 12;
            while (fifth  <= chordRoot) fifth  += 12;
            while (third  >  chordRoot + 12) third  -= 12;
            while (fifth  >  chordRoot + 12) fifth  -= 12;

            std::vector<i32> intervals;
            intervals.push_back(0);
            if (third != chordRoot) intervals.push_back(third - chordRoot);
            if (fifth != chordRoot && fifth != third) intervals.push_back(fifth - chordRoot);

            // Optional 7th for richer harmony (higher saturation → 7th chord).
            if (secSat > 0.4f && numChordsInSec >= 2) {
                i32 seventh = SnapMidiToScale(chordRoot + 10, root, scale);
                while (seventh <= chordRoot + (fifth - chordRoot)) seventh += 12;
                if (seventh <= chordRoot + 14)
                    intervals.push_back(seventh - chordRoot);
            }

            // Voicing: cinematic/ethereal goes one octave lower for warmth.
            i32 voiceShift = 0;
            if (style == CompStyle::Cinematic || style == CompStyle::Ethereal)
                voiceShift = -12;
            if (sec.type == SectionType::Intro || sec.type == SectionType::Breakdown)
                voiceShift -= 12;

            ChordEvent ev;
            ev.startBeat = sec.startBeat + static_cast<f32>(i) * beatsPerChord;
            ev.duration  = beatsPerChord;
            ev.velocity  = 0.30f + secBri * 0.25f + sec.energy * 0.2f;

            for (i32 iv : intervals)
                ev.midiNotes.push_back(chordRoot + voiceShift + iv);

            // In chorus, add a higher octave root for fullness (capped at MIDI 72).
            if (sec.type == SectionType::Chorus) {
                i32 top = chordRoot + voiceShift + 12;
                if (top <= 72) ev.midiNotes.push_back(top);
            }

            secChords.push_back(ev);
            chords.push_back(ev);
        }

        regionChords[sec.regionIdx] = std::move(secChords);
    }

    return chords;
}


// ═══════════════════════════════════════════════════════════════════════════
// BuildMelody — motif-based with repetition, variation, call-and-response
// ═══════════════════════════════════════════════════════════════════════════

std::vector<NoteEvent> ImageComposer::BuildMelody(
    const ImageData& img, const ImageAnalysis& a,
    const std::vector<ChordEvent>& chords,
    i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style)
{
    std::vector<NoteEvent> melody;
    if (!img.IsLoaded() || chords.empty()) return melody;

    auto sections = BuildStructure(img, a, totalBeats, style);
    u32 rng = static_cast<u32>(a.color.dominantHue * 100 + a.color.saturation * 1000
                               + a.edges.density * 500);

    // ── Extract motifs per unique region ─────────────────────────────
    std::unordered_map<u32, Motif> regionMotifs;
    for (auto& sec : sections) {
        if (regionMotifs.count(sec.regionIdx)) continue;

        f32 regionT = sec.startBeat / totalBeats;
        u32 imgX = static_cast<u32>(regionT * img.Width());
        u32 imgW = std::max(1u, static_cast<u32>((sec.endBeat - sec.startBeat)
                   / totalBeats * img.Width()));
        if (imgX + imgW > img.Width()) imgW = img.Width() - imgX;

        regionMotifs[sec.regionIdx] = ExtractMotif(img, imgX, imgW, root, scale, style, rng);
    }

    // Style parameters.
    // Melody octave offset from root: keep melody in a comfortable mid register.
    // Root is typically around MIDI 48-59 (C3-B3), so +12 = C4-B4, +24 = C5-B5.
    // Cinematic: one octave up (not two) to avoid piercing highs.
    i32 melodyOctave;
    f32 restProbBase;
    // Max interval excursion in semitones from the center pitch (constrains range).
    i32 maxExcursion;
    switch (style) {
    case CompStyle::Ethereal:  melodyOctave = 12; restProbBase = 0.30f; maxExcursion = 7;  break;
    case CompStyle::Harmonic:  melodyOctave = 12; restProbBase = 0.15f; maxExcursion = 10; break;
    case CompStyle::Rhythmic:  melodyOctave = 12; restProbBase = 0.18f; maxExcursion = 8;  break;
    case CompStyle::Melodic:   melodyOctave = 12; restProbBase = 0.08f; maxExcursion = 12; break;
    case CompStyle::Cinematic: melodyOctave = 12; restProbBase = 0.35f; maxExcursion = 7;  break;
    default:                   melodyOctave = 12; restProbBase = 0.15f; maxExcursion = 10; break;
    }
    // Melody center pitch — used to keep melody from drifting too high.
    i32 melodyCenterMidi = SnapMidiToScale(root + melodyOctave, root, scale);

    // ── Generate melody per section ─────────────────────────────────
    i32 prevMidi = root + melodyOctave;
    u32 repeatCount = 0;   // how many times this region's motif has been used

    for (size_t secIdx = 0; secIdx < sections.size(); ++secIdx) {
        auto& sec = sections[secIdx];
        f32 secLen = sec.endBeat - sec.startBeat;
        f32 beat = sec.startBeat;

        // Count how many times we've seen this region before.
        repeatCount = 0;
        for (size_t prev = 0; prev < secIdx; ++prev) {
            if (sections[prev].regionIdx == sec.regionIdx) ++repeatCount;
        }

        // Intro/Outro: sparse or no melody.
        if (sec.type == SectionType::Intro) {
            beat = sec.endBeat; // skip
            continue;
        }
        if (sec.type == SectionType::Outro) {
            // Sparse: just first few motif notes, fading.
            auto it = regionMotifs.find(sec.regionIdx);
            if (it != regionMotifs.end()) {
                auto& m = it->second;
                u32 notesToPlay = std::min(static_cast<u32>(m.intervals.size()), 3u);
                for (u32 n = 0; n < notesToPlay && beat < sec.endBeat; ++n) {
                    i32 midi = SnapMidiToScale(root + melodyOctave + m.intervals[n], root, scale);
                    f32 dur = m.durations[n];
                    if (beat + dur > sec.endBeat) dur = sec.endBeat - beat;
                    f32 fadeVel = m.velocities[n] * (1.0f - (beat - sec.startBeat) / secLen) * 0.5f;
                    melody.push_back({beat, dur * 0.85f, midi, std::clamp(fadeVel, 0.1f, 0.8f)});
                    prevMidi = midi;
                    beat += dur;
                }
            }
            continue;
        }

        // Breakdown: very sparse — one long note per 4 beats.
        if (sec.type == SectionType::Breakdown) {
            while (beat < sec.endBeat) {
                const ChordEvent* curChord = nullptr;
                for (auto& ch : chords)
                    if (ch.startBeat <= beat) curChord = &ch;
                i32 midi = curChord && !curChord->midiNotes.empty()
                    ? SnapMidiToScale(curChord->midiNotes[0] + 12, root, scale)
                    : root + melodyOctave;
                f32 dur = std::min(4.0f, sec.endBeat - beat);
                melody.push_back({beat, dur * 0.9f, midi, 0.25f});
                prevMidi = midi;
                beat += 4.0f;
            }
            continue;
        }

        // Drop: silence at very start, then impactful notes.
        if (sec.isDrop && beat == sec.startBeat) {
            beat += 1.0f;  // silence gap for impact
        }

        // ── Main motif-based melody generation ──────────────────────
        auto it = regionMotifs.find(sec.regionIdx);
        if (it == regionMotifs.end()) continue;
        auto& motif = it->second;

        // Fill section with motif repetitions + variations.
        u32 phraseCount = 0;
        while (beat < sec.endBeat) {
            bool isAnswer = (phraseCount % 2 == 1);  // call-and-response
            bool isVariation = (repeatCount > 0 || phraseCount > 1);

            // Play motif notes.
            for (u32 n = 0; n < motif.intervals.size() && beat < sec.endBeat; ++n) {
                // Variation: transpose for repeats (small-interval transpositions).
                i32 transposeShift = 0;
                if (isVariation) {
                    // Shift by one scale step (2 semitones), not more.
                    transposeShift = (repeatCount % 3 == 1) ? 2 : (repeatCount % 3 == 2) ? -2 : 0;
                    if (isAnswer) transposeShift = 0; // answer phrase stays in same range
                }

                i32 interval = motif.intervals[n] + transposeShift;
                i32 targetMidi = root + melodyOctave + interval;

                // Section type affects register only slightly.
                if (sec.type == SectionType::Chorus) targetMidi += 2;
                if (sec.type == SectionType::Bridge) targetMidi -= 2;

                // Hard clamp to melodyCenterMidi ± maxExcursion to prevent runaway highs.
                targetMidi = std::clamp(targetMidi,
                    melodyCenterMidi - maxExcursion,
                    melodyCenterMidi + maxExcursion);

                // Constrain stepwise motion from previous note (max 7 semitones per step).
                if (targetMidi > prevMidi + 7) targetMidi = prevMidi + 7;
                if (targetMidi < prevMidi - 7) targetMidi = prevMidi - 7;

                // On strong beats, pull toward chord tone.
                bool strongBeat = (std::fmod(beat, 4.0f) < 0.05f);
                if (strongBeat) {
                    const ChordEvent* curChord = nullptr;
                    for (auto& ch : chords)
                        if (ch.startBeat <= beat) curChord = &ch;
                    if (curChord && !curChord->midiNotes.empty()) {
                        i32 bestTone = targetMidi, bestDist = 999;
                        for (i32 cn : curChord->midiNotes) {
                            for (int oct = -1; oct <= 2; ++oct) {
                                i32 cand = cn + oct * 12;
                                i32 dist = std::abs(targetMidi - cand);
                                if (dist < bestDist) { bestDist = dist; bestTone = cand; }
                            }
                        }
                        targetMidi = bestTone;
                    }
                }

                // Answer resolution: last note resolves to root.
                if (isAnswer && n == motif.intervals.size() - 1) {
                    targetMidi = SnapMidiToScale(root + melodyOctave, root, scale);
                }

                targetMidi = SnapMidiToScale(targetMidi, root, scale);

                // Duration: occasional variation.
                f32 dur = motif.durations[n];
                if (isVariation && n % 3 == 0) {
                    // Double-time or half-time occasionally.
                    dur = (phraseCount % 3 == 1) ? dur * 0.5f : dur;
                }
                dur = QuantizeDuration(dur);
                if (beat + dur > sec.endBeat) dur = sec.endBeat - beat;
                if (dur < 0.125f) { beat = sec.endBeat; break; }

                // Rests: based on edge density + section.
                f32 t = beat / totalBeats;
                f32 edgeHere = SampleProfile(a.edges.verticalProfile, t);
                f32 restProb = restProbBase;
                if (edgeHere < 0.05f) restProb += 0.25f;
                if (sec.type == SectionType::Chorus) restProb *= 0.5f;  // fewer rests in chorus
                if (sec.type == SectionType::PreChorus) restProb *= 0.7f;

                if (RandF(rng) < restProb) {
                    beat += dur;
                    continue;
                }

                // Velocity from motif + section energy.
                f32 vel = motif.velocities[n] * (0.5f + sec.energy * 0.5f);
                // Build: crescendo within section.
                if (sec.isBuild) {
                    f32 secPos = (beat - sec.startBeat) / std::max(1.0f, secLen);
                    vel *= (0.5f + 0.5f * secPos);
                }
                vel = std::clamp(vel, 0.15f, 1.0f);

                // ── Chord-based lead: add harmony notes when image calls for it ──
                NoteEvent ev{beat, dur * 0.88f, targetMidi, vel, {}};

                // Sample local image saturation at this position.
                f32 localSat = 0.0f;
                {
                    f32 posT = beat / totalBeats;
                    u32 px = static_cast<u32>(posT * img.Width());
                    if (px >= img.Width()) px = img.Width() - 1;
                    u32 pw = std::max(1u, img.Width() / 32u);
                    if (px + pw > img.Width()) pw = img.Width() - px;
                    auto col = img.AverageColor(px, 0, pw, img.Height());
                    auto lhsb = ToHSB(col[0], col[1], col[2]);
                    localSat = lhsb[1];
                }

                // Chord lead probability from section + image + beat.
                f32 chordProb = localSat * 0.4f;
                if (sec.type == SectionType::Chorus || sec.type == SectionType::Drop)
                    chordProb += 0.35f;
                else if (sec.type == SectionType::PreChorus)
                    chordProb += 0.20f;
                else if (sec.type == SectionType::Verse)
                    chordProb += 0.05f;
                if (strongBeat)      chordProb += 0.15f;
                if (dur >= 1.0f)     chordProb += 0.15f;
                if (style == CompStyle::Ethereal || style == CompStyle::Cinematic)
                    chordProb += 0.15f;
                if (style == CompStyle::Rhythmic)
                    chordProb -= 0.10f;
                chordProb = std::clamp(chordProb, 0.0f, 0.80f);

                if (RandF(rng) < chordProb) {
                    if (localSat > 0.55f &&
                        (sec.type == SectionType::Chorus || sec.type == SectionType::Drop)) {
                        // Rich: third below (more natural than third above) + fifth.
                        ev.harmonize.push_back(
                            SnapMidiToScale(targetMidi - 3, root, scale));  // minor third below
                        if (dur >= 1.0f)
                            ev.harmonize.push_back(
                                SnapMidiToScale(targetMidi + 7, root, scale));
                    } else if (localSat > 0.35f) {
                        // Medium: fifth (stays in same octave — no high octave doubling).
                        ev.harmonize.push_back(
                            SnapMidiToScale(targetMidi + 7, root, scale));
                    } else {
                        // Light: third above (in-scale, no octave jump).
                        ev.harmonize.push_back(
                            SnapMidiToScale(targetMidi + 4, root, scale));
                    }
                    // More legato for chord passages.
                    ev.duration = dur * 0.94f;
                }

                melody.push_back(ev);
                prevMidi = targetMidi;
                beat += dur;
            }

            // Breath between phrases.
            beat += (style == CompStyle::Ethereal || style == CompStyle::Cinematic)
                  ? 1.0f : 0.25f;
            ++phraseCount;
        }
    }

    return melody;
}


// ═══════════════════════════════════════════════════════════════════════════
// BuildBass — section-aware: sustained in verse, punchy in chorus
// ═══════════════════════════════════════════════════════════════════════════

std::vector<NoteEvent> ImageComposer::BuildBass(
    const ImageData& img, const ImageAnalysis& a,
    const std::vector<ChordEvent>& chords,
    i32 root, const std::vector<i32>& scale, f32 totalBeats, CompStyle style)
{
    std::vector<NoteEvent> bass;
    if (chords.empty() || !img.IsLoaded()) return bass;

    auto sections = BuildStructure(img, a, totalBeats, style);

    for (auto& chord : chords) {
        if (chord.midiNotes.empty()) continue;

        // Find which section this chord belongs to.
        const Section* sec = SectionAt(sections, chord.startBeat);

        i32 bassRoot = chord.midiNotes[0] - 24;
        while (bassRoot < 28) bassRoot += 12;
        while (bassRoot > 55) bassRoot -= 12;
        i32 fifth = SnapMidiToScale(bassRoot + 7, root, scale);
        i32 third = SnapMidiToScale(bassRoot + 4, root, scale);
        while (third > bassRoot + 12) third -= 12;

        // Section-based activity level.
        float activity;
        if (!sec) {
            activity = 0.5f;
        } else {
            switch (sec->type) {
            case SectionType::Intro:     activity = 0.1f; break;
            case SectionType::Verse:     activity = 0.35f; break;
            case SectionType::PreChorus: activity = 0.5f; break;
            case SectionType::Chorus:    activity = 0.75f; break;
            case SectionType::Bridge:    activity = 0.3f; break;
            case SectionType::Drop:      activity = 0.9f; break;
            case SectionType::Breakdown: activity = 0.1f; break;
            case SectionType::Outro:     activity = 0.25f; break;
            }
            // Image modulation.
            f32 t = chord.startBeat / totalBeats;
            f32 edgeAct = SampleProfile(a.edges.verticalProfile, t);
            activity = activity * 0.6f + edgeAct * 0.4f;

            if (style == CompStyle::Ethereal || style == CompStyle::Cinematic)
                activity = std::min(activity, 0.4f);
            if (style == CompStyle::Rhythmic)
                activity = std::max(activity, 0.4f);
        }

        if (activity < 0.15f) {
            // Near-silence (intro/breakdown): one quiet sustained note.
            bass.push_back({chord.startBeat, chord.duration * 0.9f, bassRoot,
                            chord.velocity * 0.3f});
        } else if (activity < 0.35f) {
            // Sustained root.
            bass.push_back({chord.startBeat, chord.duration * 0.95f, bassRoot,
                            chord.velocity * 0.6f});
        } else if (activity < 0.5f) {
            // Root + fifth.
            f32 half = chord.duration / 2.0f;
            bass.push_back({chord.startBeat, half * 0.9f, bassRoot, chord.velocity * 0.7f});
            bass.push_back({chord.startBeat + half, half * 0.9f, fifth, chord.velocity * 0.55f});
        } else if (activity < 0.7f) {
            // Walking: root – 3rd – 5th – approach.
            f32 step = chord.duration / 4.0f;
            i32 approach = SnapMidiToScale(bassRoot - 1, root, scale);
            i32 walk[] = { bassRoot, third, fifth, approach };
            for (int j = 0; j < 4; ++j) {
                f32 bStart = chord.startBeat + j * step;
                if (bStart >= chord.startBeat + chord.duration) break;
                bass.push_back({bStart, step * 0.8f, walk[j],
                                chord.velocity * (j == 0 ? 0.75f : 0.55f)});
            }
        } else {
            // Punchy 8th-note pattern (chorus/drop).
            f32 stepLen = 0.5f;
            u32 steps = static_cast<u32>(chord.duration / stepLen);
            for (u32 j = 0; j < steps; ++j) {
                f32 bStart = chord.startBeat + j * stepLen;
                f32 t2 = bStart / totalBeats;
                f32 subEdge = SampleProfile(a.edges.verticalProfile, t2);
                i32 note = (j % 2 == 0) ? bassRoot
                         : (subEdge > 0.3f ? fifth : bassRoot + 12);
                f32 vel = (j % 2 == 0) ? chord.velocity * 0.8f : chord.velocity * 0.5f;
                bass.push_back({bStart, stepLen * 0.7f, note, vel});
            }
        }
    }

    return bass;
}


// ═══════════════════════════════════════════════════════════════════════════
// BuildDrums — section-aware density with fills at transitions
// ═══════════════════════════════════════════════════════════════════════════

std::vector<DrumHit> ImageComposer::BuildDrums(
    const ImageData& img, const ImageAnalysis& a, f32 totalBeats, CompStyle style)
{
    std::vector<DrumHit> drums;
    if (!img.IsLoaded()) return drums;

    auto sections = BuildStructure(img, a, totalBeats, style);

    // ── Groove template selection ─────────────────────────────────────
    // Use image edge density to select one of several kick patterns.
    // Kick pattern: 1 = kick on this 16th step (16 steps per 4-beat bar).
    // Standard groove: kick on 1 and 3 (steps 0, 8), snare on 2 and 4 (steps 4, 12).
    // Each pattern is 16 steps (1 bar), tiled across the composition.
    static const u8 kKickPatterns[4][16] = {
        {1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0},  // Four-on-the-floor variants
        {1,0,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,0},  // Kick: 1, 2-and, 3
        {1,0,0,0, 0,0,0,0, 1,0,0,1, 0,0,0,0},  // Kick: 1, 3, 3-and
        {1,0,1,0, 0,0,0,0, 1,0,0,0, 0,1,0,0},  // Syncopated
    };
    static const u8 kSnarePatterns[3][16] = {
        {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // Standard backbeat (beats 2 & 4)
        {0,0,0,0, 1,0,0,1, 0,0,0,0, 1,0,0,0},  // Backbeat + 2-and ghost
        {0,0,0,0, 1,0,0,0, 0,0,1,0, 1,0,0,1},  // Syncopated snare fills
    };
    static const u8 kHatPatterns[4][16] = {
        {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0},  // Straight 8ths
        {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1},  // 16th-note hats
        {1,0,0,1, 0,1,0,0, 1,0,0,1, 0,1,0,0},  // Off-beat hats (reggae/funk)
        {1,0,1,0, 0,0,1,0, 1,0,1,0, 0,0,1,0},  // Swing groove
    };

    // Choose groove based on image texture and style.
    u32 kickPatIdx  = static_cast<u32>(a.edges.density * 3.99f);
    u32 snarePatIdx = static_cast<u32>(a.texture.roughness * 2.99f);
    u32 hatPatIdx   = static_cast<u32>(a.texture.regularity * 3.99f);
    kickPatIdx  = std::min(kickPatIdx,  3u);
    snarePatIdx = std::min(snarePatIdx, 2u);
    hatPatIdx   = std::min(hatPatIdx,   3u);

    // For ethereal/cinematic: simpler patterns (kick pattern 0, sparse hats).
    if (style == CompStyle::Ethereal || style == CompStyle::Cinematic) {
        kickPatIdx  = 0;
        snarePatIdx = 0;
        hatPatIdx   = 0;
    }

    // 16th-note grid step size is always 0.25 beats.
    const f32 stepSize = 0.25f;
    u32 numSteps = static_cast<u32>(totalBeats / stepSize);

    for (u32 s = 0; s < numSteps; ++s) {
        f32 beat = s * stepSize;
        f32 t = beat / totalBeats;
        u32 step16 = s % 16;   // position within 1-bar pattern (0-15)

        const Section* sec = SectionAt(sections, beat);
        if (!sec) continue;

        f32 secLen = sec->endBeat - sec->startBeat;
        f32 secPos = (beat - sec->startBeat) / std::max(1.0f, secLen);

        // Drop: silence at start.
        if (sec->isDrop && secPos < 0.1f) continue;

        // ── Section activity level ───────────────────────────────────
        f32 densityMul;
        switch (sec->type) {
        case SectionType::Intro:     densityMul = 0.0f;  break;  // no drums in intro
        case SectionType::Verse:     densityMul = 0.7f;  break;
        case SectionType::PreChorus: densityMul = 0.85f; break;
        case SectionType::Chorus:    densityMul = 1.0f;  break;
        case SectionType::Bridge:    densityMul = 0.6f;  break;
        case SectionType::Drop:      densityMul = 1.1f;  break;
        case SectionType::Breakdown: densityMul = 0.0f;  break;  // no drums in breakdown
        case SectionType::Outro:     densityMul = 0.5f;  break;
        default:                     densityMul = 0.7f;  break;
        }

        if (style == CompStyle::Ethereal || style == CompStyle::Cinematic)
            densityMul *= 0.6f;

        // Build: gradually add density.
        if (sec->isBuild) densityMul *= (0.5f + 0.5f * secPos);

        if (densityMul < 0.05f) continue;

        f32 edgeV = SampleProfile(a.edges.verticalProfile, t);
        f32 edgeH = SampleProfile(a.edges.horizontalProfile, t);

        // ── Groove pattern lookup ────────────────────────────────────
        bool kickOn  = (kKickPatterns[kickPatIdx][step16] != 0);
        bool snareOn = (kSnarePatterns[snarePatIdx][step16] != 0);
        bool hatOn   = (kHatPatterns[hatPatIdx][step16] != 0);

        // Image modulation: low edge density can remove occasional hits.
        // Kick: strong musical anchor — keep it unless section is very sparse.
        if (kickOn && densityMul > 0.1f) {
            f32 vel = 0.7f + edgeV * 0.3f;
            // Stronger on beat 1 (step 0 of bar).
            if (step16 == 0) vel = std::min(vel + 0.1f, 1.0f);
            // Build: crescendo.
            if (sec->isBuild) vel *= (0.6f + 0.4f * secPos);
            drums.push_back({beat, DrumType::Kick, std::clamp(vel * densityMul, 0.3f, 1.0f)});
        }

        // Snare: musical backbeat — keep it consistent.
        if (snareOn && densityMul > 0.3f) {
            f32 vel = 0.65f + edgeH * 0.25f;
            if (sec->isBuild) vel *= (0.5f + 0.5f * secPos);
            drums.push_back({beat, DrumType::Snare, std::clamp(vel, 0.3f, 0.9f)});
        }

        // Hi-hat: image edge drives velocity variation, but pattern stays musical.
        if (hatOn) {
            f32 vel = 0.25f + (edgeV + edgeH) * 0.15f;
            // Open hat on off-beats in chorus/drop.
            bool isOffbeat = (step16 % 4 == 2);
            bool openHat = isOffbeat && (sec->type == SectionType::Chorus ||
                                         sec->type == SectionType::Drop) &&
                           edgeV > 0.4f;
            DrumType ht = openHat ? DrumType::OpenHat : DrumType::HiHat;
            // Fewer hats in sparse sections.
            if (densityMul > 0.4f || (step16 % 4 == 0))
                drums.push_back({beat, ht, std::clamp(vel, 0.1f, 0.5f)});
        }
    }

    // ── Transition fills: drum roll at end of each section ──────────
    for (size_t si = 0; si + 1 < sections.size(); ++si) {
        auto& sec = sections[si];
        // Only add fill if going into chorus/drop or if it's a build.
        auto nextType = sections[si + 1].type;
        bool needsFill = (nextType == SectionType::Chorus || nextType == SectionType::Drop)
                       || sec.isBuild;
        if (!needsFill) continue;

        // Fill: last 2 beats of section, accelerating snare hits.
        f32 fillStart = sec.endBeat - 2.0f;
        if (fillStart < sec.startBeat) fillStart = sec.startBeat;

        f32 fillRes = 0.25f;
        for (f32 fb = fillStart; fb < sec.endBeat; fb += fillRes) {
            f32 intensity = (fb - fillStart) / 2.0f;  // 0 to 1
            f32 vel = 0.3f + intensity * 0.5f;
            drums.push_back({fb, DrumType::Snare, vel});
            // Add kick on last hit for impact.
            if (fb + fillRes >= sec.endBeat)
                drums.push_back({fb, DrumType::Kick, 0.9f});
        }
    }

    // Ensure minimal pulse in chorus/verse sections (beat 1 of each bar if missing).
    // With the groove patterns this rarely fires, but ensures no silent bars.
    for (auto& sec : sections) {
        if (sec.type == SectionType::Breakdown || sec.type == SectionType::Intro) continue;
        for (f32 b = sec.startBeat; b < sec.endBeat; b += 4.0f) {
            bool hasKick = false;
            for (auto& d : drums) {
                if (std::abs(d.beat - b) < 0.1f && d.type == DrumType::Kick) {
                    hasKick = true; break;
                }
            }
            if (!hasKick && sec.type != SectionType::Breakdown)
                drums.push_back({b, DrumType::Kick, 0.5f});
        }
    }

    std::sort(drums.begin(), drums.end(),
        [](const DrumHit& x, const DrumHit& y) { return x.beat < y.beat; });

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
    f32 newFreq = MidiToFreq(static_cast<f32>(n.midiNote));
    f32 durSamples = n.duration * 60.0f / (comp_.tempo * tempoScale_) * sampleRate_;
    f32 releaseTime = comp_.melodyRelease;

    // Legato/portamento: if a voice is active and note is close, glide instead of retrigger.
    if (comp_.portamentoRate > 0.01f) {
        for (auto& v : melodyVoices_) {
            if (v.active && v.envStage != Release) {
                i32 interval = std::abs(n.midiNote - static_cast<i32>(
                    std::round(12.0f * std::log2(v.freq / 440.0f) + 69.0f)));
                if (interval <= 7) {
                    // Legato: glide to new note.
                    v.targetFreq  = newFreq;
                    v.glideRate   = 1.0f / (comp_.portamentoRate * 0.1f * sampleRate_);
                    v.amp         = n.velocity;
                    v.samplesLeft = static_cast<u32>(durSamples);
                    v.envStage    = Sustain;

                    // Glide harmony voices too.
                    u32 hIdx = 0;
                    for (auto& hv : melodyVoices_) {
                        if (&hv == &v) continue;
                        if (hIdx < n.harmonize.size() && hv.active && hv.envStage != Release) {
                            f32 hFreq = MidiToFreq(static_cast<f32>(n.harmonize[hIdx]));
                            hv.targetFreq  = hFreq;
                            hv.glideRate   = v.glideRate;
                            hv.amp         = n.velocity * 0.65f;
                            hv.samplesLeft = static_cast<u32>(durSamples);
                            hv.envStage    = Sustain;
                            ++hIdx;
                        }
                    }
                    // Release leftover harmony voices if chord shrunk.
                    return;
                }
            }
        }
    }

    // Normal trigger: find quietest voice for primary note.
    Voice* best = &melodyVoices_[0];
    for (auto& v : melodyVoices_) {
        if (!v.active) { best = &v; break; }
        if (v.envLevel < best->envLevel) best = &v;
    }
    best->active      = true;
    best->freq        = newFreq;
    best->targetFreq  = newFreq;
    best->glideRate   = 0;
    best->amp         = n.velocity;
    best->phase       = 0;
    best->phase2      = 0;
    best->envLevel    = 0;
    best->envStage    = Attack;
    best->attackRate  = 1.0f / (comp_.melodyAttack * sampleRate_);
    best->releaseRate = 1.0f / (releaseTime * sampleRate_);
    best->samplesLeft = static_cast<u32>(durSamples);
    best->pan         = 0.0f;
    best->filterState = 0;

    // Trigger additional voices for harmony notes (chord lead).
    for (size_t h = 0; h < n.harmonize.size(); ++h) {
        Voice* hv = nullptr;
        for (auto& v : melodyVoices_) {
            if (&v == best) continue; // skip primary
            if (!v.active) { hv = &v; break; }
        }
        if (!hv) {
            // Steal quietest non-primary voice.
            hv = nullptr;
            for (auto& v : melodyVoices_) {
                if (&v == best) continue;
                if (!hv || v.envLevel < hv->envLevel) hv = &v;
            }
        }
        if (!hv) break;

        f32 hFreq = MidiToFreq(static_cast<f32>(n.harmonize[h]));
        hv->active      = true;
        hv->freq        = hFreq;
        hv->targetFreq  = hFreq;
        hv->glideRate   = 0;
        hv->amp         = n.velocity * 0.6f; // harmony slightly quieter
        hv->phase       = 0.1 * static_cast<f64>(h + 1); // slight phase offset for width
        hv->phase2      = 0;
        hv->envLevel    = 0;
        hv->envStage    = Attack;
        hv->attackRate  = 1.0f / (std::max(comp_.melodyAttack, 0.02f) * sampleRate_);
        hv->releaseRate = 1.0f / (releaseTime * 1.2f * sampleRate_);
        hv->samplesLeft = static_cast<u32>(durSamples);
        hv->pan         = (h == 0) ? -0.25f : 0.25f; // slight stereo spread
        hv->filterState = 0;
    }
}

void MusicRenderer::TriggerChord(const ChordEvent& c) {
    for (auto& v : chordVoices_) {
        if (v.active && v.envStage != Release) v.envStage = Release;
    }

    u32 voiceIdx = 0;
    for (i32 midiNote : c.midiNotes) {
        if (voiceIdx >= kChordVoices) break;

        Voice* best = nullptr;
        for (auto& v : chordVoices_) {
            if (!v.active) { best = &v; break; }
        }
        if (!best) {
            best = &chordVoices_[0];
            for (auto& v : chordVoices_) {
                if (v.envLevel < best->envLevel) best = &v;
            }
        }

        f32 freq = MidiToFreq(static_cast<f32>(midiNote));
        best->active      = true;
        best->freq        = freq;
        best->targetFreq  = freq;
        best->glideRate   = 0;
        best->amp         = c.velocity;
        best->phase       = 0;
        best->phase2      = 0.3;
        best->envLevel    = 0;
        best->envStage    = Attack;
        best->attackRate  = 1.0f / (comp_.padAttack * sampleRate_);
        best->releaseRate = 1.0f / (1.0f * sampleRate_);
        best->samplesLeft = static_cast<u32>(c.duration * 60.0f / (comp_.tempo * tempoScale_) * sampleRate_);

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
    f32 freq = MidiToFreq(static_cast<f32>(n.midiNote));
    best->active      = true;
    best->freq        = freq;
    best->targetFreq  = freq;
    best->glideRate   = 0;
    best->amp         = n.velocity;
    best->phase       = 0;
    best->envLevel    = 0;
    best->envStage    = Attack;
    best->attackRate  = 1.0f / (0.01f * sampleRate_);
    best->releaseRate = 1.0f / (0.15f * sampleRate_);
    best->samplesLeft = static_cast<u32>(n.duration * 60.0f / (comp_.tempo * tempoScale_) * sampleRate_);
    best->pan         = 0;
    best->filterState = 0;
}

void MusicRenderer::TriggerDrum(const DrumHit& h) {
    DrumVoice* best = nullptr;
    for (auto& v : drumVoices_) {
        if (!v.active) { best = &v; break; }
    }
    if (!best) {
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


// ── Voice envelope ──────────────────────────────────────────────────────────

void MusicRenderer::UpdateVoice(Voice& v) {
    if (!v.active) return;

    // Portamento frequency glide.
    if (v.glideRate > 0 && std::abs(v.freq - v.targetFreq) > 0.01f) {
        v.freq += (v.targetFreq - v.freq) * v.glideRate;
    }

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

    f32 p = static_cast<f32>(v.phase);
    f32 sample = 0;

    auto& hp = comp_.harmonicProfile;
    if (hp.empty()) {
        sample = std::sin(kTwoPI * p);
    } else {
        // Use only first 4 harmonics for melody to keep it warm and clear.
        u32 numH = std::min(static_cast<u32>(hp.size()), 4u);
        for (u32 h = 0; h < numH; ++h) {
            sample += hp[h] * std::sin(kTwoPI * static_cast<f32>(h + 1) * p);
        }
    }

    if (comp_.noiseBlend > 0.001f) {
        sample = sample * (1.0f - comp_.noiseBlend) + Noise() * comp_.noiseBlend * 0.3f;
    }

    sample *= v.envLevel * v.amp * 0.4f;

    // Two-pole resonant-style lowpass: tighter cutoff to prevent harsh highs.
    // Cutoff is proportional to frequency so higher notes get relatively more roll-off.
    f32 cutoff = std::clamp(0.06f + brightness_ * 0.35f, 0.02f, 0.5f);
    v.filterState += cutoff * (sample - v.filterState);
    f32 filtered2 = v.filterState;
    filtered2 += cutoff * (v.filterState - filtered2);
    sample = v.filterState;

    v.phase += static_cast<f64>(v.freq) / sampleRate_;
    if (v.phase >= 1.0) v.phase -= 1.0;

    UpdateVoice(v);
    return sample;
}

void MusicRenderer::RenderChordVoice(Voice& v, f32& left, f32& right) {
    if (!v.active) return;

    f32 p1 = static_cast<f32>(v.phase);
    f32 p2 = static_cast<f32>(v.phase2);

    f32 s1 = 0, s2 = 0;
    auto& hp = comp_.harmonicProfile;
    if (hp.empty()) {
        s1 = std::sin(kTwoPI * p1);
        s2 = std::sin(kTwoPI * p2);
    } else {
        // Pads: only first 3 harmonics with strong rolloff — rich but warm.
        u32 numH = std::min(static_cast<u32>(hp.size()), 3u);
        for (u32 h = 0; h < numH; ++h) {
            f32 weight = hp[h] * (1.0f / static_cast<f32>((h + 1) * (h + 1)));
            s1 += weight * std::sin(kTwoPI * static_cast<f32>(h + 1) * p1);
            s2 += weight * std::sin(kTwoPI * static_cast<f32>(h + 1) * p2);
        }
    }

    f32 sample = (s1 + s2) * 0.2f * v.envLevel * v.amp;

    // Tight lowpass for pads — remove high-frequency content aggressively.
    f32 cutoff = std::clamp(0.04f + brightness_ * 0.25f, 0.02f, 0.35f);
    if (comp_.filterSweep > 0.001f) {
        f32 lfo = 0.5f + 0.5f * std::sin(kTwoPI * static_cast<f32>(v.phase * 0.05));
        cutoff += comp_.filterSweep * lfo * 0.05f;
        cutoff = std::clamp(cutoff, 0.02f, 0.45f);
    }
    v.filterState += cutoff * (sample - v.filterState);
    sample = v.filterState;

    f32 panL = std::cos((v.pan + 1.0f) * kHalfPI * 0.5f);
    f32 panR = std::sin((v.pan + 1.0f) * kHalfPI * 0.5f);
    left  += sample * panL;
    right += sample * panR;

    f32 detune = 1.0f + comp_.detuneAmount;
    v.phase  += static_cast<f64>(v.freq) / sampleRate_;
    v.phase2 += static_cast<f64>(v.freq * detune) / sampleRate_;
    if (v.phase  >= 1.0) v.phase  -= 1.0;
    if (v.phase2 >= 1.0) v.phase2 -= 1.0;

    UpdateVoice(v);
}

f32 MusicRenderer::RenderBassVoice(Voice& v) {
    if (!v.active) return 0;

    f32 p = static_cast<f32>(v.phase);

    f32 sample = 0;
    auto& hp = comp_.harmonicProfile;
    if (hp.empty()) {
        sample = std::sin(kTwoPI * p);
    } else {
        u32 numH = std::min(static_cast<u32>(hp.size()), 4u);
        for (u32 h = 0; h < numH; ++h) {
            f32 weight = hp[h] * (1.0f / static_cast<f32>((h + 1) * (h + 1)));
            sample += weight * std::sin(kTwoPI * static_cast<f32>(h + 1) * p);
        }
    }

    sample *= 0.6f * v.envLevel * v.amp;

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
        // Pitch sweep: 150Hz → 40Hz as envelope decays (punchy sub kick).
        f32 freq = 40.0f + 110.0f * v.env * v.env;
        sample = std::sin(kTwoPI * static_cast<f32>(v.phase)) * v.env;
        v.phase += static_cast<f64>(freq) / sampleRate_;
        decayRate = 1.0f - 4.0f / sampleRate_;
        break;
    }
    case DrumType::Snare: {
        // Body: 200Hz sine + noise burst filtered for snap.
        f32 body = std::sin(kTwoPI * static_cast<f32>(v.phase)) * v.env * 0.45f;
        v.phase += 200.0 / sampleRate_;
        f32 noise = Noise() * v.env;
        // High-pass the noise to get snare rattle (subtract low-pass).
        v.filterState += 0.4f * (noise - v.filterState);
        f32 rattle = (noise - v.filterState) * 0.55f;
        sample = body + rattle;
        decayRate = 1.0f - 9.0f / sampleRate_;
        break;
    }
    case DrumType::HiHat: {
        f32 noise = Noise();
        // High-pass: subtract a strong LP to get a bright, metallic hi-hat.
        v.filterState += 0.2f * (noise - v.filterState);
        sample = (noise - v.filterState) * v.env * 0.35f;
        decayRate = 1.0f - 35.0f / sampleRate_;
        break;
    }
    case DrumType::OpenHat: {
        f32 noise = Noise();
        v.filterState += 0.15f * (noise - v.filterState);
        sample = (noise - v.filterState) * v.env * 0.3f;
        decayRate = 1.0f - 6.0f / sampleRate_;
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

        // ── Trigger events ──────────────────────────────────────────
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

        // ── Render all layers ───────────────────────────────────────
        f32 melL = 0, melR = 0;
        for (auto& v : melodyVoices_) {
            f32 s = RenderMelodyVoice(v);
            f32 panL = std::cos((v.pan + 1.0f) * kHalfPI * 0.5f);
            f32 panR = std::sin((v.pan + 1.0f) * kHalfPI * 0.5f);
            melL += s * panL;
            melR += s * panR;
        }

        f32 chordL = 0, chordR = 0;
        for (auto& v : chordVoices_) RenderChordVoice(v, chordL, chordR);

        f32 bassSample = 0;
        for (auto& v : bassVoices_) bassSample += RenderBassVoice(v);

        f32 drumSample = 0;
        for (auto& v : drumVoices_) drumSample += RenderDrumVoice(v);

        // ── Apply per-beat dynamics from section-aware map ──────────
        f32 dynGain = 1.0f;
        if (!comp_.dynamicMap.empty()) {
            u32 beatIdx = static_cast<u32>(loopBeat);
            if (beatIdx >= comp_.dynamicMap.size())
                beatIdx = static_cast<u32>(comp_.dynamicMap.size()) - 1;
            dynGain = comp_.dynamicMap[beatIdx];
        }

        // ── Mix ─────────────────────────────────────────────────────
        f32 left  = melL       * comp_.melodyGain
                  + chordL     * comp_.chordGain
                  + bassSample * comp_.bassGain
                  + drumSample * comp_.drumGain;
        f32 right = melR       * comp_.melodyGain
                  + chordR     * comp_.chordGain
                  + bassSample * comp_.bassGain
                  + drumSample * comp_.drumGain;

        f32 eg = energy_;
        left  *= (0.3f + eg * 0.7f) * dynGain;
        right *= (0.3f + eg * 0.7f) * dynGain;

        // Master lowpass — keep it warm, cap at 0.6 to always roll off some highs.
        f32 masterCutoff = std::clamp(0.25f + brightness_ * 0.35f, 0.1f, 0.6f);
        masterFilterL_ += masterCutoff * (left  - masterFilterL_);
        masterFilterR_ += masterCutoff * (right - masterFilterR_);

        left  = std::tanh(masterFilterL_ * 1.2f);
        right = std::tanh(masterFilterR_ * 1.2f);

        output->SetSample(0, f, left);
        if (output->Channels() > 1) output->SetSample(1, f, right);

        prevBeat_ = loopBeat;
        ++totalSamples_;
    }
}

} // namespace sky
