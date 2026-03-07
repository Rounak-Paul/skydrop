#pragma once

#include <cstdint>
#include <string>
#include <vector>

class AudioPlayer {
public:
    static int  Init();
    static void Shutdown();

    // Call once per frame: services streaming buffers and detects track end.
    static void Update();

    // Async decode + load.  Returns immediately; playback begins on the audio
    // thread once decoding is complete.  TrackChangedEvent is emitted from the
    // audio thread when ready (safe — Event::Emit is mutex-protected).
    static void Load(const std::string& path);

    static void Play();
    static void Pause();
    static void Resume();
    static void Stop();
    static void Seek(float seconds);
    static void SetVolume(float volume);  // 0.0 – 1.0

    static float GetPosition();           // seconds elapsed
    static float GetDuration();           // total track length in seconds
    static bool  IsPlaying();
    static bool  IsPaused();

    // Album art decoded to RGBA8 pixels.  Copies the data out — safe to call
    // from any thread after a successful Load() that contained artwork.
    // Returns false (and leaves pixels empty) when no art is available.
    static bool CopyAlbumArt(std::vector<uint8_t>& pixels, int& width, int& height);

    // ID3 / file-format metadata (returned by value — safe from any thread)
    static std::string GetTitle();
    static std::string GetArtist();
    static std::string GetAlbum();

    // ---- Waveform -----------------------------------------------------------
    // 1024 normalised mono RMS peaks [0,1].  Populated after each Load().
    // WaveformReadyEvent is emitted from the audio thread when ready.
    static bool CopyWaveform(std::vector<float>& peaks);

    // ---- Spatial / HRTF audio -----------------------------------------------
    enum class SpatialPreset { Off, Room, ConcertHall, OpenAir };

    static void          SetSpatialPreset(SpatialPreset p);
    static SpatialPreset GetSpatialPreset();

    // Azimuth in degrees: 0 = front, +90 = right, ±180 = behind.
    static void  SetSpatialAzimuth(float degrees);
    static float GetSpatialAzimuth();

    // ---- Equalizer ----------------------------------------------------------
    // Linear gains in [0.126, 7.943]  (≈ −18 dB … +18 dB). Default 1.0 = flat.
    static void SetEQBands(float bassGain, float midGain, float trebleGain);
    static void GetEQBands(float& bassGain, float& midGain, float& trebleGain);
};
