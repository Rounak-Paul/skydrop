#pragma once

/// @file mixer.h
/// @brief Mixer node: sums multiple audio inputs with individual gain and pan.

#include "skydrop/core/audio_node.h"

namespace sky {

/// Mixes up to 8 stereo audio inputs into one stereo output.
class MixerNode : public AudioNode {
public:
    static constexpr u32 kMaxInputs = 8;

    MixerNode() {
        for (u32 i = 0; i < kMaxInputs; ++i)
            AddInput("In " + std::to_string(i + 1));
        AddOutput("Audio");
        AddParam("MasterGain", 1.0f, 0.0f, 2.0f);
    }

    const char* TypeName() const override { return "Mixer"; }
    const char* Category() const override { return "Utility"; }

    void Process(const std::vector<const SampleBuffer*>& inputs,
                 std::vector<SampleBuffer*>& outputs) override {
        if (outputs.empty()) return;
        auto* out = outputs[0];
        f32 masterGain = GetParam(0);

        for (u32 f = 0; f < bufferSize_; ++f) {
            for (u32 c = 0; c < out->Channels(); ++c) {
                f32 sum = 0.0f;
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (inputs[i]) sum += inputs[i]->Sample(c, f);
                }
                out->SetSample(c, f, sum * masterGain);
            }
        }
    }
};

/// Simple gain node.
class GainNode : public AudioNode {
public:
    GainNode() {
        AddInput("Audio");
        AddOutput("Audio");
        AddParam("Gain", 1.0f, 0.0f, 4.0f);
        AddParam("Pan",  0.0f, -1.0f, 1.0f);  // -1 = full left, +1 = full right
    }

    const char* TypeName() const override { return "Gain"; }
    const char* Category() const override { return "Utility"; }

    void Process(const std::vector<const SampleBuffer*>& inputs,
                 std::vector<SampleBuffer*>& outputs) override {
        if (inputs.empty() || !inputs[0] || outputs.empty()) return;
        const auto* in = inputs[0];
        auto* out = outputs[0];
        f32 gain = GetParam(0);
        f32 pan  = GetParam(1);

        // Constant-power pan law.
        f32 leftGain  = gain * std::cos((pan + 1.0f) * kHalfPI / 2.0f);
        f32 rightGain = gain * std::sin((pan + 1.0f) * kHalfPI / 2.0f);

        for (u32 f = 0; f < bufferSize_; ++f) {
            if (in->Channels() >= 2 && out->Channels() >= 2) {
                out->SetSample(0, f, in->Sample(0, f) * leftGain);
                out->SetSample(1, f, in->Sample(1, f) * rightGain);
            } else {
                for (u32 c = 0; c < out->Channels(); ++c)
                    out->SetSample(c, f, in->Sample(c, f) * gain);
            }
        }
    }
};

/// Output node — terminal node that the audio engine reads from.
class OutputNode : public AudioNode {
public:
    OutputNode() {
        AddInput("Audio");
        AddOutput("Audio"); // pass-through for monitoring
        AddParam("Volume", 0.8f, 0.0f, 1.0f);
    }

    const char* TypeName() const override { return "Output"; }
    const char* Category() const override { return "Utility"; }

    void Process(const std::vector<const SampleBuffer*>& inputs,
                 std::vector<SampleBuffer*>& outputs) override {
        if (inputs.empty() || !inputs[0] || outputs.empty()) return;
        const auto* in = inputs[0];
        auto* out = outputs[0];
        f32 vol = GetParam(0);

        for (u32 f = 0; f < bufferSize_; ++f)
            for (u32 c = 0; c < out->Channels(); ++c)
                out->SetSample(c, f, in->Sample(c, f) * vol);
    }
};

} // namespace sky
