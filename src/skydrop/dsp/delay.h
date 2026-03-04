#pragma once

/// @file delay.h
/// @brief Delay line effect node.

#include "skydrop/core/audio_node.h"

namespace sky {

/// Simple stereo delay with feedback.
class DelayNode : public AudioNode {
public:
    DelayNode() {
        AddInput("Audio");
        AddOutput("Audio");
        AddParam("Time",     0.3f,  0.001f, 2.0f);    // seconds
        AddParam("Feedback", 0.4f,  0.0f,   0.95f);
        AddParam("Mix",      0.5f,  0.0f,   1.0f);    // dry/wet
    }

    const char* TypeName() const override { return "Delay"; }
    const char* Category() const override { return "Effect"; }

    void Init(u32 sampleRate, u32 bufferSize) override {
        AudioNode::Init(sampleRate, bufferSize);
        u32 maxDelaySamples = sampleRate * 2; // 2 seconds max
        for (auto& buf : delayBuf_) buf.assign(maxDelaySamples, 0.0f);
    }

    void Reset() override {
        for (auto& buf : delayBuf_) std::fill(buf.begin(), buf.end(), 0.0f);
        writePos_ = 0;
    }

    void Process(const std::vector<const SampleBuffer*>& inputs,
                 std::vector<SampleBuffer*>& outputs) override {
        if (inputs.empty() || !inputs[0] || outputs.empty()) return;

        const auto* in = inputs[0];
        auto* out = outputs[0];
        f32 delayTime = GetParam(0);
        f32 feedback  = GetParam(1);
        f32 mix       = GetParam(2);

        u32 delaySamples = static_cast<u32>(delayTime * sampleRate_);
        u32 bufLen = static_cast<u32>(delayBuf_[0].size());
        delaySamples = std::min(delaySamples, bufLen - 1);

        for (u32 f = 0; f < bufferSize_; ++f) {
            u32 readPos = (writePos_ + bufLen - delaySamples) % bufLen;

            for (u32 c = 0; c < std::min(in->Channels(), 2u); ++c) {
                f32 dry = in->Sample(c, f);
                f32 delayed = delayBuf_[c][readPos];
                delayBuf_[c][writePos_] = dry + delayed * feedback;
                out->SetSample(c, f, dry * (1.0f - mix) + delayed * mix);
            }
            writePos_ = (writePos_ + 1) % bufLen;
        }
    }

private:
    std::array<std::vector<f32>, 2> delayBuf_;
    u32 writePos_ = 0;
};

} // namespace sky
