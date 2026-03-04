#pragma once

/// @file audio_engine.h
/// @brief Real-time audio output via OpenAL-soft.
/// Manages the OpenAL device/context and streams audio from the AudioGraph.

#include "skydrop/core/audio_graph.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <thread>
#include <atomic>
#include <mutex>

namespace sky {

/// OpenAL streaming audio engine.
/// Pulls audio from an AudioGraph and streams it to the speakers.
class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine() { Shutdown(); }

    /// Initialize the audio engine.
    bool Init(u32 sampleRate = kDefaultSampleRate,
              u32 bufferSize = kDefaultBufferSize,
              u32 numBuffers = 4);

    /// Shutdown and release all OpenAL resources.
    void Shutdown();

    /// Set the audio graph to pull audio from.
    void SetGraph(AudioGraph* graph) { graph_ = graph; }

    /// Start audio playback.
    void Play();

    /// Pause audio playback.
    void Pause();

    /// Stop and reset.
    void Stop();

    /// Is the engine currently playing?
    bool IsPlaying() const { return playing_.load(); }

    /// Get current sample rate.
    u32 GetSampleRate() const { return sampleRate_; }

    /// Get current buffer size.
    u32 GetBufferSize() const { return bufferSize_; }

    /// Get peak level (for metering). Returns left/right.
    std::pair<f32, f32> GetPeakLevel() const { return {peakL_.load(), peakR_.load()}; }

    /// Reset peak meters.
    void ResetPeakMeters() { peakL_ = 0; peakR_ = 0; }

    /// Get the output node's latest buffer (for waveform display).
    const SampleBuffer& GetLastOutputBuffer() const { return lastOutput_; }

private:
    void StreamThread();
    void FillBuffer(ALuint buffer);

    ALCdevice*  device_  = nullptr;
    ALCcontext* context_ = nullptr;
    ALuint      source_  = 0;
    std::vector<ALuint> buffers_;

    AudioGraph* graph_ = nullptr;

    u32 sampleRate_ = kDefaultSampleRate;
    u32 bufferSize_ = kDefaultBufferSize;
    u32 numBuffers_ = 4;

    std::thread streamThread_;
    std::atomic<bool> playing_{false};
    std::atomic<bool> shutdown_{false};
    std::mutex graphMutex_;

    SampleBuffer lastOutput_;
    std::atomic<f32> peakL_{0}, peakR_{0};
};

} // namespace sky
