#pragma once

#include "event.h"
#include "ui/music_events.h"

enum class RepeatMode { None, One, All };

class MusicQueue {
public:
    static void Init();
    static void Shutdown();

    static bool     IsShuffled()  { return _shuffle; }
    static RepeatMode RepeatMode_() { return _repeat; }

private:
    static void PlayIndex(int32_t index);
    static void BroadcastQueueChanged();

    static std::vector<QueueChangedEvent::Entry> _tracks;
    static int32_t    _currentIndex;
    static bool       _shuffle;
    static RepeatMode _repeat;

    // Event listener IDs
    static ListenerID _idEnqueue;
    static ListenerID _idPlayTrack;
    static ListenerID _idSkipNext;
    static ListenerID _idSkipPrev;
    static ListenerID _idTrackEnded;
    static ListenerID _idStop;
    static ListenerID _idClear;
    static ListenerID _idRemove;
    static ListenerID _idShuffle;
    static ListenerID _idRepeat;
};
