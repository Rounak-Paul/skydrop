#pragma once

/// @file envelope.h
/// @brief ADSR envelope generator node.

#include "skydrop/core/audio_node.h"

namespace sky {

/// ADSR envelope stages.
enum class EnvStage : u8 { Idle, Attack, Decay, Sustain, Release };

/// Classic ADSR envelope generator.
/// Can be triggered via a Trigger input or programmatically.
class EnvelopeNode : public AudioNode {
public:
    EnvelopeNode() {
        AddInput("Trigger", PortType::Trigger);
        AddInput("Audio");
        AddOutput("Audio");
        AddOutput("Envelope", PortType::Control);
        AddParam("Attack",  0.01f, 0.001f, 10.0f);   // seconds
        AddParam("Decay",   0.1f,  0.001f, 10.0f);
        AddParam("Sustain", 0.7f,  0.0f,   1.0f);
        AddParam("Release", 0.3f,  0.001f, 10.0f);
    }

    const char* TypeName() const override { return "Envelope"; }
    const char* Category() const override { return "Modulation"; }

    void Reset() override {
        stage_ = EnvStage::Idle;
        level_ = 0.0f;
    }

    /// Programmatic gate control.
    void Gate(bool on) {
        if (on && stage_ == EnvStage::Idle) {
            stage_ = EnvStage::Attack;
        } else if (on && stage_ == EnvStage::Release) {
            stage_ = EnvStage::Attack;
        } else if (!on && stage_ != EnvStage::Idle) {
            stage_ = EnvStage::Release;
        }
    }

    /// Trigger a note-on (starts Attack).
    void NoteOn() { Gate(true); }

    /// Trigger a note-off (starts Release).
    void NoteOff() { Gate(false); }

    EnvStage CurrentStage() const { return stage_; }
    f32      CurrentLevel() const { return level_; }

    void Process(const std::vector<const SampleBuffer*>& inputs,
                 std::vector<SampleBuffer*>& outputs) override {
        f32 attack   = GetParam(0);
        f32 decay    = GetParam(1);
        f32 sustain  = GetParam(2);
        f32 release  = GetParam(3);

        f32 attackRate  = 1.0f / (attack * sampleRate_);
        f32 decayRate   = 1.0f / (decay * sampleRate_);
        f32 releaseRate = 1.0f / (release * sampleRate_);

        const SampleBuffer* audioIn = (inputs.size() > 1) ? inputs[1] : nullptr;
        SampleBuffer* audioOut = (outputs.size() > 0) ? outputs[0] : nullptr;
        SampleBuffer* envOut   = (outputs.size() > 1) ? outputs[1] : nullptr;

        for (u32 f = 0; f < bufferSize_; ++f) {
            // Advance envelope.
            switch (stage_) {
            case EnvStage::Attack:
                level_ += attackRate;
                if (level_ >= 1.0f) { level_ = 1.0f; stage_ = EnvStage::Decay; }
                break;
            case EnvStage::Decay:
                level_ -= decayRate * (1.0f - sustain);
                if (level_ <= sustain) { level_ = sustain; stage_ = EnvStage::Sustain; }
                break;
            case EnvStage::Sustain:
                level_ = sustain;
                break;
            case EnvStage::Release:
                level_ -= releaseRate * level_;
                if (level_ <= 0.001f) { level_ = 0.0f; stage_ = EnvStage::Idle; }
                break;
            case EnvStage::Idle:
                level_ = 0.0f;
                break;
            }

            // Apply envelope to audio input, or just output the envelope.
            for (u32 c = 0; c < kDefaultChannels; ++c) {
                f32 inSample = (audioIn) ? audioIn->Sample(c, f) : 1.0f;
                if (audioOut) audioOut->SetSample(c, f, inSample * level_);
            }
            if (envOut) {
                for (u32 c = 0; c < envOut->Channels(); ++c)
                    envOut->SetSample(c, f, level_);
            }
        }
    }

private:
    EnvStage stage_ = EnvStage::Idle;
    f32      level_ = 0.0f;
};

} // namespace sky
