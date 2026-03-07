#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Add file paths to the end of the queue
struct EnqueueTracksEvent {
    std::vector<std::string> paths;
};

// Jump to a specific index in the queue
struct PlayTrackEvent {
    int32_t index;
};

// Skip one track forward / backward
struct SkipNextEvent {};
struct SkipPrevEvent {};

// Toggle play / pause
struct PauseToggleEvent {};

// Stop playback and reset position
struct StopEvent {};

// Seek to an absolute playback position
struct SeekEvent {
    float posSeconds;
};

// Volume: 0.0 – 1.0
struct VolumeChangeEvent {
    float volume;
};

// Toggle shuffle (randomises unplayed portion of queue)
struct ShuffleToggleEvent {};

// Cycle repeat mode: None → One → All
struct RepeatToggleEvent {};

// Remove all entries from the queue
struct QueueClearEvent {};

// Remove one entry by its current queue index
struct RemoveTrackEvent {
    int32_t index;
};

// Fired by AudioPlayer when the current track reaches its end
struct TrackEndedEvent {};

// Fired by MusicQueue whenever the active track changes
struct TrackChangedEvent {
    std::string title;
    std::string artist;
    std::string album;
    float       durationSeconds = 0.0f;
    bool        hasAlbumArt     = false;
};

// Fired every frame while the player is active
struct PlaybackTickEvent {
    float posSeconds;
    float durSeconds;
    bool  isPlaying;
    bool  isPaused;
};

// Fired when waveform peak data for the loaded track is ready
struct WaveformReadyEvent {};

// Fired by MusicQueue whenever the queue contents or current index change
struct QueueChangedEvent {
    struct Entry {
        std::string path;
        std::string title;
        std::string artist;
        float       durationSeconds = 0.0f;
    };
    std::vector<Entry> tracks;
    int32_t            currentIndex = -1;
};
