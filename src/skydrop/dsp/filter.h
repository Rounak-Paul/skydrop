#pragma once

/// @file filter.h
/// @brief Biquad filter node (low-pass, high-pass, band-pass, notch).

#include "skydrop/core/audio_node.h"

namespace sky {

enum class FilterType : i32 { LowPass = 0, HighPass, BandPass, Notch, Count };

/// Standard biquad IIR filter.
class FilterNode : public AudioNode {
public:
    FilterNode() {
        AddInput("Audio");
        AddOutput("Audio");
        AddParam("Cutoff",    1000.0f, 20.0f, 20000.0f);
        AddParam("Resonance", 0.707f, 0.1f, 20.0f);
        AddEnumParam("Type", {"LowPass", "HighPass", "BandPass", "Notch"}, 0);
    }

    const char* TypeName() const override { return "Filter"; }
    const char* Category() const override { return "Effect"; }

    void Reset() override {
        for (auto& s : state_) s = {};
    }

    void Init(u32 sampleRate, u32 bufferSize) override {
        AudioNode::Init(sampleRate, bufferSize);
        state_.resize(kDefaultChannels);
    }

    void Process(const std::vector<const SampleBuffer*>& inputs,
                 std::vector<SampleBuffer*>& outputs) override {
        if (inputs.empty() || !inputs[0] || outputs.empty()) return;

        const auto* in  = inputs[0];
        auto* out       = outputs[0];
        f32 cutoff      = GetParam(0);
        f32 q           = GetParam(1);
        auto type       = static_cast<FilterType>(static_cast<i32>(GetParam(2)));

        ComputeCoeffs(type, cutoff, q);

        for (u32 c = 0; c < in->Channels(); ++c) {
            auto& s = state_[c];
            for (u32 f = 0; f < bufferSize_; ++f) {
                f32 x = in->Sample(c, f);
                f32 y = b0_ * x + b1_ * s.x1 + b2_ * s.x2 - a1_ * s.y1 - a2_ * s.y2;
                s.x2 = s.x1; s.x1 = x;
                s.y2 = s.y1; s.y1 = y;
                out->SetSample(c, f, y);
            }
        }
    }

private:
    void ComputeCoeffs(FilterType type, f32 cutoff, f32 q) {
        f32 w0 = kTwoPI * cutoff / static_cast<f32>(sampleRate_);
        f32 alpha = std::sin(w0) / (2.0f * q);
        f32 cosw0 = std::cos(w0);

        f32 a0;
        switch (type) {
        case FilterType::LowPass:
            b0_ = (1.0f - cosw0) / 2.0f;
            b1_ =  1.0f - cosw0;
            b2_ = (1.0f - cosw0) / 2.0f;
            a0  =  1.0f + alpha;
            a1_ = -2.0f * cosw0;
            a2_ =  1.0f - alpha;
            break;
        case FilterType::HighPass:
            b0_ =  (1.0f + cosw0) / 2.0f;
            b1_ = -(1.0f + cosw0);
            b2_ =  (1.0f + cosw0) / 2.0f;
            a0  =   1.0f + alpha;
            a1_ =  -2.0f * cosw0;
            a2_ =   1.0f - alpha;
            break;
        case FilterType::BandPass:
            b0_ =  alpha;
            b1_ =  0.0f;
            b2_ = -alpha;
            a0  =  1.0f + alpha;
            a1_ = -2.0f * cosw0;
            a2_ =  1.0f - alpha;
            break;
        case FilterType::Notch:
            b0_ =  1.0f;
            b1_ = -2.0f * cosw0;
            b2_ =  1.0f;
            a0  =  1.0f + alpha;
            a1_ = -2.0f * cosw0;
            a2_ =  1.0f - alpha;
            break;
        default: a0 = 1.0f; break;
        }

        // Normalize.
        b0_ /= a0; b1_ /= a0; b2_ /= a0;
        a1_ /= a0; a2_ /= a0;
    }

    struct BiquadState { f32 x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::vector<BiquadState> state_;
    f32 b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
};

} // namespace sky
