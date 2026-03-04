#pragma once

/// @file reverb.h
/// @brief Simple Schroeder reverb node using comb and allpass filters.

#include "skydrop/core/audio_node.h"

namespace sky {

/// Schroeder reverb with 4 parallel comb filters and 2 series allpass filters.
class ReverbNode : public AudioNode {
public:
    ReverbNode() {
        AddInput("Audio");
        AddOutput("Audio");
        AddParam("RoomSize", 0.7f, 0.0f, 1.0f);
        AddParam("Damping",  0.5f, 0.0f, 1.0f);
        AddParam("Mix",      0.3f, 0.0f, 1.0f);
    }

    const char* TypeName() const override { return "Reverb"; }
    const char* Category() const override { return "Effect"; }

    void Init(u32 sampleRate, u32 bufferSize) override {
        AudioNode::Init(sampleRate, bufferSize);
        // Comb filter delay lengths (in samples, scaled for sample rate).
        f32 scale = static_cast<f32>(sampleRate) / 44100.0f;
        combLen_[0] = static_cast<u32>(1116 * scale);
        combLen_[1] = static_cast<u32>(1188 * scale);
        combLen_[2] = static_cast<u32>(1277 * scale);
        combLen_[3] = static_cast<u32>(1356 * scale);
        apLen_[0]   = static_cast<u32>(556  * scale);
        apLen_[1]   = static_cast<u32>(441  * scale);

        for (int i = 0; i < 4; ++i) combBuf_[i].assign(combLen_[i], 0.0f);
        for (int i = 0; i < 2; ++i) apBuf_[i].assign(apLen_[i], 0.0f);
        std::fill(combPos_, combPos_ + 4, 0u);
        std::fill(apPos_, apPos_ + 2, 0u);
        std::fill(combFilter_, combFilter_ + 4, 0.0f);
    }

    void Reset() override {
        for (auto& b : combBuf_) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : apBuf_)   std::fill(b.begin(), b.end(), 0.0f);
        std::fill(combPos_, combPos_ + 4, 0u);
        std::fill(apPos_, apPos_ + 2, 0u);
        std::fill(combFilter_, combFilter_ + 4, 0.0f);
    }

    void Process(const std::vector<const SampleBuffer*>& inputs,
                 std::vector<SampleBuffer*>& outputs) override {
        if (inputs.empty() || !inputs[0] || outputs.empty()) return;
        const auto* in = inputs[0];
        auto* out = outputs[0];
        f32 roomSize = GetParam(0);
        f32 damping  = GetParam(1);
        f32 mix      = GetParam(2);

        f32 feedback = roomSize * 0.9f + 0.1f; // map 0..1 → 0.1..1.0

        for (u32 f = 0; f < bufferSize_; ++f) {
            // Mono sum for reverb processing.
            f32 mono = 0.0f;
            for (u32 c = 0; c < in->Channels(); ++c) mono += in->Sample(c, f);
            mono /= static_cast<f32>(in->Channels());

            // Parallel comb filters.
            f32 combOut = 0.0f;
            for (int i = 0; i < 4; ++i) {
                f32 delayed = combBuf_[i][combPos_[i]];
                combFilter_[i] = delayed * (1.0f - damping) + combFilter_[i] * damping;
                combBuf_[i][combPos_[i]] = mono + combFilter_[i] * feedback;
                combPos_[i] = (combPos_[i] + 1) % combLen_[i];
                combOut += delayed;
            }
            combOut *= 0.25f;

            // Series allpass filters.
            f32 apOut = combOut;
            for (int i = 0; i < 2; ++i) {
                f32 delayed = apBuf_[i][apPos_[i]];
                f32 temp = -apOut * 0.5f + delayed;
                apBuf_[i][apPos_[i]] = apOut + delayed * 0.5f;
                apPos_[i] = (apPos_[i] + 1) % apLen_[i];
                apOut = temp;
            }

            // Mix and write output.
            for (u32 c = 0; c < out->Channels(); ++c) {
                f32 dry = in->Sample(c, f);
                out->SetSample(c, f, dry * (1.0f - mix) + apOut * mix);
            }
        }
    }

private:
    std::vector<f32> combBuf_[4];
    std::vector<f32> apBuf_[2];
    u32 combLen_[4] = {};
    u32 apLen_[2]   = {};
    u32 combPos_[4] = {};
    u32 apPos_[2]   = {};
    f32 combFilter_[4] = {};
};

} // namespace sky
