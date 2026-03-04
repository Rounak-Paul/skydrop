#pragma once
#include "skydrop/synth/image_composer.h"

namespace sky {

class ColorHarmonyNode : public AudioNode {
public:
    ColorHarmonyNode() {
        AddOutput("Audio");
        AddParam("Tempo",        0.5f, 0.0f, 1.0f);
        AddParam("Brightness",   0.5f, 0.0f, 1.0f);
        AddParam("Energy",       0.5f, 0.0f, 1.0f);
        AddParam("Stereo Width", 0.7f, 0.0f, 1.0f);
    }

    const char* TypeName() const override { return "Harmonic"; }
    const char* Category() const override { return "Image"; }

    void SetImageData(const ImageData& img, const ImageAnalysis& a) {
        comp_ = ImageComposer::Compose(img, a, CompStyle::Harmonic);
        renderer_.SetComposition(comp_);
    }

    void Init(u32 sampleRate, u32 bufferSize) override {
        AudioNode::Init(sampleRate, bufferSize);
        renderer_.Init(sampleRate);
    }

    void Reset() override { renderer_.Reset(); }

    void Process(const std::vector<const SampleBuffer*>&,
                 std::vector<SampleBuffer*>& outputs) override {
        if (outputs.empty()) return;
        renderer_.SetTempoScale(0.5f + GetParam(0));
        renderer_.SetBrightness(GetParam(1));
        renderer_.SetEnergy(GetParam(2));
        renderer_.SetStereoWidth(GetParam(3));
        renderer_.Process(outputs[0], bufferSize_);
    }

private:
    Composition   comp_;
    MusicRenderer renderer_;
};

} // namespace sky
