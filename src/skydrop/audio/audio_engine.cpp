/// @file audio_engine.cpp
/// @brief OpenAL-soft audio streaming implementation.

#include "skydrop/audio/audio_engine.h"

#include <chrono>
#include <cstring>

namespace sky {

bool AudioEngine::Init(u32 sampleRate, u32 bufferSize, u32 numBuffers) {
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;
    numBuffers_ = numBuffers;

    // Open default device.
    device_ = alcOpenDevice(nullptr);
    if (!device_) return false;

    context_ = alcCreateContext(device_, nullptr);
    if (!context_) {
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }
    alcMakeContextCurrent(context_);

    // Generate source.
    alGenSources(1, &source_);
    if (alGetError() != AL_NO_ERROR) {
        Shutdown();
        return false;
    }

    // Generate streaming buffers.
    buffers_.resize(numBuffers);
    alGenBuffers(static_cast<ALsizei>(numBuffers), buffers_.data());
    if (alGetError() != AL_NO_ERROR) {
        Shutdown();
        return false;
    }

    lastOutput_.Resize(kDefaultChannels, bufferSize);
    return true;
}

void AudioEngine::Shutdown() {
    Stop();

    if (source_) {
        alSourceStop(source_);
        alSourcei(source_, AL_BUFFER, 0);
        alDeleteSources(1, &source_);
        source_ = 0;
    }

    if (!buffers_.empty()) {
        alDeleteBuffers(static_cast<ALsizei>(buffers_.size()), buffers_.data());
        buffers_.clear();
    }

    if (context_) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context_);
        context_ = nullptr;
    }

    if (device_) {
        alcCloseDevice(device_);
        device_ = nullptr;
    }
}

void AudioEngine::Play() {
    if (playing_.load()) return;
    if (!device_ || !context_) return;

    playing_ = true;
    shutdown_ = false;

    // Pre-fill all buffers.
    for (auto buf : buffers_) FillBuffer(buf);

    // Queue buffers and start playback.
    alSourceQueueBuffers(source_, static_cast<ALsizei>(buffers_.size()), buffers_.data());
    alSourcePlay(source_);

    // Start streaming thread.
    streamThread_ = std::thread(&AudioEngine::StreamThread, this);
}

void AudioEngine::Pause() {
    if (!playing_.load()) return;
    playing_ = false;
    shutdown_ = true;
    if (streamThread_.joinable()) streamThread_.join();
    alSourcePause(source_);
}

void AudioEngine::Stop() {
    if (playing_.load() || shutdown_.load()) {
        playing_ = false;
        shutdown_ = true;
        if (streamThread_.joinable()) streamThread_.join();
    }

    if (source_) {
        alSourceStop(source_);
        // Unqueue all buffers.
        ALint queued = 0;
        alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0) {
            ALuint buf;
            alSourceUnqueueBuffers(source_, 1, &buf);
        }
    }

    if (graph_) graph_->ResetAll();
}

void AudioEngine::StreamThread() {
    while (!shutdown_.load()) {
        // Check for processed buffers.
        ALint processed = 0;
        alGetSourcei(source_, AL_BUFFERS_PROCESSED, &processed);

        while (processed-- > 0) {
            ALuint buf;
            alSourceUnqueueBuffers(source_, 1, &buf);
            FillBuffer(buf);
            alSourceQueueBuffers(source_, 1, &buf);
        }

        // Ensure source is still playing (might underrun).
        ALint state;
        alGetSourcei(source_, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && playing_.load()) {
            alSourcePlay(source_);
        }

        // Sleep briefly to avoid busy-waiting.
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

void AudioEngine::FillBuffer(ALuint buffer) {
    // Process the audio graph.
    SampleBuffer graphOutput(kDefaultChannels, bufferSize_);

    {
        std::lock_guard<std::mutex> lock(graphMutex_);
        if (graph_) {
            graph_->Process();

            // Find the output node and read its buffer.
            NodeId outId = graph_->FindOutputNode();
            const SampleBuffer* outBuf = graph_->GetOutputBuffer(outId, 0);
            if (outBuf) {
                graphOutput.CopyFrom(*outBuf);
            }
        }
    }

    // Store for visualization.
    lastOutput_.CopyFrom(graphOutput);

    // Compute peak levels.
    f32 peakL = 0, peakR = 0;
    for (u32 f = 0; f < bufferSize_; ++f) {
        peakL = std::max(peakL, std::abs(graphOutput.Sample(0, f)));
        if (graphOutput.Channels() > 1)
            peakR = std::max(peakR, std::abs(graphOutput.Sample(1, f)));
    }
    peakL_ = peakL;
    peakR_ = peakR;

    // Interleave and convert to 16-bit PCM for OpenAL.
    std::vector<i16> pcm(static_cast<size_t>(bufferSize_) * kDefaultChannels);
    for (u32 f = 0; f < bufferSize_; ++f) {
        for (u32 c = 0; c < kDefaultChannels; ++c) {
            f32 sample = Clamp(graphOutput.Sample(c, f), -1.0f, 1.0f);
            pcm[f * kDefaultChannels + c] = static_cast<i16>(sample * 32767.0f);
        }
    }

    ALenum format = (kDefaultChannels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    alBufferData(buffer, format, pcm.data(),
                 static_cast<ALsizei>(pcm.size() * sizeof(i16)),
                 static_cast<ALsizei>(sampleRate_));
}

} // namespace sky
