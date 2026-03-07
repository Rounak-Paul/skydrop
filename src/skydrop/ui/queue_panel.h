#pragma once

#include "event.h"
#include "ui/music_events.h"
#include <string>
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
};
