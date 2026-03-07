#pragma once

#include <cstdint>
#include <string>

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

    // Album art decoded to RGBA8 pixels. Valid only after a successful Load()
    // that contained embedded artwork. Callers must not free this pointer.
    static const uint8_t* GetAlbumArtPixels();
    static int            GetAlbumArtWidth();
    static int            GetAlbumArtHeight();
    static bool           HasAlbumArt();

    // ID3 / file-format metadata (returned by value — safe from any thread)
    static std::string GetTitle();
    static std::string GetArtist();
    static std::string GetAlbum();
};
