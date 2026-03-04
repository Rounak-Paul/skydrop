#pragma once

/// @file sample_buffer.h
/// @brief Interleaved or per-channel audio sample buffer.

#include "skydrop/core/types.h"

namespace sky {

/// A flexible audio buffer that holds floating-point samples.
/// Can be used as mono, stereo, or multi-channel.
class SampleBuffer {
public:
    SampleBuffer() = default;

    /// Construct with a given number of channels and frames.
    SampleBuffer(u32 channels, u32 frames)
        : channels_(channels), frames_(frames),
          data_(static_cast<size_t>(channels) * frames, 0.0f) {}

    /// Resize, clearing all data.
    void Resize(u32 channels, u32 frames) {
        channels_ = channels;
        frames_ = frames;
        data_.assign(static_cast<size_t>(channels) * frames, 0.0f);
    }

    /// Zero out all samples.
    void Clear() { std::fill(data_.begin(), data_.end(), 0.0f); }

    /// Get a mutable pointer to the start of a channel's data.
    f32* Channel(u32 ch) {
        assert(ch < channels_);
        return data_.data() + static_cast<size_t>(ch) * frames_;
    }

    /// Get a const pointer to the start of a channel's data.
    const f32* Channel(u32 ch) const {
        assert(ch < channels_);
        return data_.data() + static_cast<size_t>(ch) * frames_;
    }

    /// Read a single sample.
    f32 Sample(u32 ch, u32 frame) const {
        return data_[static_cast<size_t>(ch) * frames_ + frame];
    }

    /// Write a single sample.
    void SetSample(u32 ch, u32 frame, f32 value) {
        data_[static_cast<size_t>(ch) * frames_ + frame] = value;
    }

    /// Add to a sample (mix).
    void AddSample(u32 ch, u32 frame, f32 value) {
        data_[static_cast<size_t>(ch) * frames_ + frame] += value;
    }

    /// Apply gain to entire buffer.
    void ApplyGain(f32 gain) {
        for (auto& s : data_) s *= gain;
    }

    /// Mix another buffer into this one (additive).
    void MixFrom(const SampleBuffer& other, f32 gain = 1.0f) {
        assert(channels_ == other.channels_ && frames_ == other.frames_);
        for (size_t i = 0; i < data_.size(); ++i)
            data_[i] += other.data_[i] * gain;
    }

    /// Copy from another buffer.
    void CopyFrom(const SampleBuffer& other) {
        channels_ = other.channels_;
        frames_ = other.frames_;
        data_ = other.data_;
    }

    /// Interleave channels into a single output array (for audio output).
    /// Output size must be >= channels * frames.
    void Interleave(f32* output) const {
        for (u32 f = 0; f < frames_; ++f)
            for (u32 c = 0; c < channels_; ++c)
                output[f * channels_ + c] = data_[static_cast<size_t>(c) * frames_ + f];
    }

    /// De-interleave from an interleaved input array.
    void Deinterleave(const f32* input, u32 channels, u32 frames) {
        Resize(channels, frames);
        for (u32 f = 0; f < frames; ++f)
            for (u32 c = 0; c < channels; ++c)
                data_[static_cast<size_t>(c) * frames + f] = input[f * channels + c];
    }

    /// Get peak amplitude across all channels.
    f32 PeakAmplitude() const {
        f32 peak = 0.0f;
        for (auto s : data_) peak = std::max(peak, std::abs(s));
        return peak;
    }

    /// Normalize buffer to a target peak amplitude.
    void Normalize(f32 targetPeak = 1.0f) {
        f32 peak = PeakAmplitude();
        if (peak > 1e-10f) ApplyGain(targetPeak / peak);
    }

    u32 Channels() const { return channels_; }
    u32 Frames()   const { return frames_; }
    size_t Size()  const { return data_.size(); }
    f32* Data()          { return data_.data(); }
    const f32* Data() const { return data_.data(); }
    bool Empty()   const { return data_.empty(); }

    /// Duration in seconds at a given sample rate.
    f32 Duration(u32 sampleRate = kDefaultSampleRate) const {
        return static_cast<f32>(frames_) / static_cast<f32>(sampleRate);
    }

private:
    u32 channels_ = 0;
    u32 frames_   = 0;
    std::vector<f32> data_;
};

} // namespace sky
