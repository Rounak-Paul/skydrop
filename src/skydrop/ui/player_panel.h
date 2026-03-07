#pragma once

#include "event.h"
#include <tinyvk/renderer/texture.h>
#include <memory>
#include <string>
#include <vector>

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
    static ListenerID _idQueueChanged;
    static ListenerID _idWaveform;

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

    // Queue state
    static int32_t _queueIndex;
    static int32_t _queueSize;

    // Album art texture (Vulkan-backed, rendered via ImGui)
    static tvk::Ref<tvk::Texture> _artTexture;
    static bool _pendingRebuildArt;
    static int  _artTexW;
    static int  _artTexH;

    // Waveform (1024 normalized RMS peaks)
    static std::vector<float> _waveform;
    static std::string        _currentTrackPath;  // for annotation lookup

    // Annotation popup state
    static bool  _showAnnotPopup;
    static float _annotPopupPos;    // seconds
    static char  _annotInputBuf[256];
};
