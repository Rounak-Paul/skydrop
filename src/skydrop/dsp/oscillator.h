#pragma once

/// @file oscillator.h
/// @brief Audio oscillator node with multiple waveforms.

#include "skydrop/core/audio_node.h"

namespace sky {

/// Oscillator waveforms.
enum class Waveform : i32 { Sine = 0, Saw, Square, Triangle, WhiteNoise, Count };

/// A basic audio oscillator node.
/// Params: Frequency, Amplitude, Waveform, Detune, Phase
/// Outputs: Audio
class OscillatorNode : public AudioNode {
public:
    OscillatorNode() {
        AddOutput("Audio");
        AddParam("Frequency", 440.0f, 20.0f, 20000.0f);
        AddParam("Amplitude", 0.5f, 0.0f, 1.0f);
        AddEnumParam("Waveform", {"Sine", "Saw", "Square", "Triangle", "Noise"}, 0);
        AddParam("Detune", 0.0f, -100.0f, 100.0f);  // cents
        AddParam("PulseWidth", 0.5f, 0.01f, 0.99f);
    }

    const char* TypeName() const override { return "Oscillator"; }
    const char* Category() const override { return "Source"; }

    void Reset() override { phase_ = 0.0; }

    void Process(const std::vector<const SampleBuffer*>& /*inputs*/,
                 std::vector<SampleBuffer*>& outputs) override {
        if (outputs.empty()) return;
        auto* out = outputs[0];

        f32 freq  = GetParam(0);
        f32 amp   = GetParam(1);
        auto wave = static_cast<Waveform>(static_cast<i32>(GetParam(2)));
        f32 detune = GetParam(3);
        f32 pw     = GetParam(4);

        // Apply detune in cents.
        f32 actualFreq = freq * std::pow(2.0f, detune / 1200.0f);
        f64 phaseInc = static_cast<f64>(actualFreq) / static_cast<f64>(sampleRate_);

        for (u32 f = 0; f < bufferSize_; ++f) {
            f32 sample = GenerateSample(wave, phase_, pw);
            sample *= amp;

            // Write to all channels.
            for (u32 c = 0; c < out->Channels(); ++c)
                out->SetSample(c, f, sample);

            phase_ += phaseInc;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }

private:
    static f32 GenerateSample(Waveform wave, f64 phase, f32 pw) {
        f32 p = static_cast<f32>(phase);
        switch (wave) {
        case Waveform::Sine:
            return std::sin(kTwoPI * p);
        case Waveform::Saw:
            return 2.0f * p - 1.0f;
        case Waveform::Square:
            return (p < pw) ? 1.0f : -1.0f;
        case Waveform::Triangle:
            return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
        case Waveform::WhiteNoise:
            return static_cast<f32>(rand()) / static_cast<f32>(RAND_MAX) * 2.0f - 1.0f;
        default:
            return 0.0f;
        }
    }

    f64 phase_ = 0.0;
};

/// Multi-oscillator that can produce additive overtones (for richer timbre).
class AdditiveOscNode : public AudioNode {
public:
    AdditiveOscNode() {
        AddOutput("Audio");
        AddParam("BaseFreq", 220.0f, 20.0f, 8000.0f);
        AddParam("Amplitude", 0.5f, 0.0f, 1.0f);
        AddParam("NumHarmonics", 8.0f, 1.0f, 32.0f);
        AddParam("HarmonicDecay", 0.7f, 0.0f, 1.0f);  // amplitude multiplier per harmonic
        AddParam("Brightness", 1.0f, 0.0f, 2.0f);      // exponent on harmonic number
    }

    const char* TypeName() const override { return "AdditiveOsc"; }
    const char* Category() const override { return "Source"; }

    void Reset() override { std::fill(phases_.begin(), phases_.end(), 0.0); }

    void Init(u32 sampleRate, u32 bufferSize) override {
        AudioNode::Init(sampleRate, bufferSize);
        phases_.resize(32, 0.0);
    }

    void Process(const std::vector<const SampleBuffer*>& /*inputs*/,
                 std::vector<SampleBuffer*>& outputs) override {
        if (outputs.empty()) return;
        auto* out = outputs[0];

        f32 baseFreq = GetParam(0);
        f32 amp      = GetParam(1);
        i32 numH     = static_cast<i32>(GetParam(2));
        f32 decay    = GetParam(3);
        f32 bright   = GetParam(4);

        for (u32 f = 0; f < bufferSize_; ++f) {
            f32 sample = 0.0f;
            f32 totalAmp = 0.0f;

            for (i32 h = 1; h <= numH; ++h) {
                f32 harmAmp = std::pow(decay, static_cast<f32>(h - 1))
                            * std::pow(1.0f / static_cast<f32>(h), bright);
                sample += harmAmp * std::sin(kTwoPI * static_cast<f32>(phases_[h - 1]));
                totalAmp += harmAmp;

                f64 freq = static_cast<f64>(baseFreq * h);
                phases_[h - 1] += freq / static_cast<f64>(sampleRate_);
                if (phases_[h - 1] >= 1.0) phases_[h - 1] -= 1.0;
            }

            if (totalAmp > 0.0f) sample /= totalAmp;
            sample *= amp;

            for (u32 c = 0; c < out->Channels(); ++c)
                out->SetSample(c, f, sample);
        }
    }

private:
    std::vector<f64> phases_;
};

} // namespace sky
