#pragma once

#include "event.h"
#include <tinyvk/renderer/texture.h>
#include <memory>
#include <string>

class PlayerPanel {
public:
    static void Init();
    static void Shutdown();
    static void OnUI();

private:
    static void RebuildArtTexture();

    // Listener IDs
    static ListenerID _idTrackChanged;
    static ListenerID _idTick;

    // Cached playback state (updated from PlaybackTickEvent)
    static float  _pos;
    static float  _dur;
    static bool   _playing;
    static bool   _paused;

    // Cached metadata
    static std::string _title;
    static std::string _artist;
    static std::string _album;
    static float       _volume;

    // Album art texture (Vulkan-backed, rendered via ImGui)
    static tvk::Ref<tvk::Texture> _artTexture;
    static bool _pendingRebuildArt;
};
