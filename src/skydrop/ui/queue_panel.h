#pragma once

#include "event.h"
#include "ui/music_events.h"
#include <tinyvk/renderer/texture.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class QueuePanel {
public:
    static void Init();
    static void Shutdown();
    static void OnUI();

private:
    static ListenerID _idQueueChanged;

    static std::vector<QueueChangedEvent::Entry> _tracks;
    static int32_t _currentIndex;

    // ---- Thumbnail cache ---------------------------------------------------
    enum class ThumbState { Pending, Loaded, NoArt };
    struct ThumbEntry {
        ThumbState             state = ThumbState::Pending;
        tvk::Ref<tvk::Texture> tex;
        int                    texW  = 0;
        int                    texH  = 0;
    };

    struct PendingUpload {
        std::string          path;
        std::vector<uint8_t> pixels;
        int                  w = 0, h = 0;
    };

    static std::unordered_map<std::string, ThumbEntry> _thumbs;
    static std::mutex                                   _thumbsMutex;
    static std::vector<PendingUpload>                   _pendingUploads;
    static std::mutex                                   _pendingMutex;

    // Schedule art extraction for any path not yet in the cache
    static void ScheduleThumbnail(const std::string& path);
    // Upload pending GPU textures (call from main thread each frame)
    static void FlushPendingUploads();
    // Free all cached thumbnails
    static void ClearThumbs();
};
