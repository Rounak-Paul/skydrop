#include "music_queue.h"
#include "audio_player.h"

#include <algorithm>
#include <filesystem>
#include <random>

// ---- Statics ------------------------------------------------------------

std::vector<QueueChangedEvent::Entry> MusicQueue::_tracks;
int32_t    MusicQueue::_currentIndex = -1;
bool       MusicQueue::_shuffle      = false;
RepeatMode MusicQueue::_repeat       = RepeatMode::None;

ListenerID MusicQueue::_idEnqueue   = 0;
ListenerID MusicQueue::_idPlayTrack = 0;
ListenerID MusicQueue::_idSkipNext  = 0;
ListenerID MusicQueue::_idSkipPrev  = 0;
ListenerID MusicQueue::_idTrackEnded= 0;
ListenerID MusicQueue::_idStop      = 0;
ListenerID MusicQueue::_idClear     = 0;
ListenerID MusicQueue::_idRemove    = 0;
ListenerID MusicQueue::_idShuffle   = 0;
ListenerID MusicQueue::_idRepeat    = 0;

// ---- Helpers ------------------------------------------------------------

void MusicQueue::BroadcastQueueChanged() {
    Event::Emit(QueueChangedEvent{ _tracks, _currentIndex });
}

void MusicQueue::PlayIndex(int32_t index) {
    if (_tracks.empty()) return;
    if (index < 0) index = static_cast<int32_t>(_tracks.size()) - 1;
    if (index >= static_cast<int32_t>(_tracks.size())) index = 0;

    _currentIndex = index;
    BroadcastQueueChanged();

    // Kick off async decode+play. TrackChangedEvent will be emitted from the
    // audio thread once decoding is complete — MusicQueue does NOT read
    // metadata synchronously here because it isn't ready yet.
    AudioPlayer::Load(_tracks[_currentIndex].path);
}

// Handle TrackChangedEvent to backfill resolved metadata into the queue entry.
static ListenerID s_idTrackChangedMeta = 0;

// ---- Init / Shutdown ----------------------------------------------------

void MusicQueue::Init() {
    // Backfill resolved metadata into the queue entry once the audio thread
    // finishes decoding (it emits TrackChangedEvent with the real title etc.)
    s_idTrackChangedMeta = Event::Register<TrackChangedEvent>([](const TrackChangedEvent& e) {
        if (_currentIndex < 0 || _currentIndex >= static_cast<int32_t>(_tracks.size())) return;
        auto& entry = _tracks[_currentIndex];
        if (!e.title.empty())          entry.title           = e.title;
        if (!e.artist.empty())         entry.artist          = e.artist;
        if (e.durationSeconds > 0.0f)  entry.durationSeconds = e.durationSeconds;
        MusicQueue::BroadcastQueueChanged();
    });

    _idEnqueue = Event::Register<EnqueueTracksEvent>([](const EnqueueTracksEvent& e) {
        bool startPlaying = _tracks.empty();
        for (const auto& p : e.paths) {
            QueueChangedEvent::Entry entry;
            entry.path  = p;
            entry.title = std::filesystem::path(p).stem().string();
            _tracks.push_back(std::move(entry));
        }
        BroadcastQueueChanged();
        if (startPlaying) PlayIndex(0);
    });

    _idPlayTrack = Event::Register<PlayTrackEvent>([](const PlayTrackEvent& e) {
        PlayIndex(e.index);
    });

    _idSkipNext = Event::Register<SkipNextEvent>([](const SkipNextEvent&) {
        if (_tracks.empty()) return;
        if (_repeat == RepeatMode::One) { PlayIndex(_currentIndex); return; }
        int32_t next = _currentIndex + 1;
        if (next >= static_cast<int32_t>(_tracks.size())) {
            if (_repeat == RepeatMode::All) next = 0;
            else { AudioPlayer::Stop(); return; }
        }
        PlayIndex(next);
    });

    _idSkipPrev = Event::Register<SkipPrevEvent>([](const SkipPrevEvent&) {
        // If more than 3 seconds in, restart current track
        if (AudioPlayer::GetPosition() > 3.0f) { AudioPlayer::Seek(0.0f); return; }
        int32_t prev = _currentIndex - 1;
        if (prev < 0) prev = _repeat == RepeatMode::All ? static_cast<int32_t>(_tracks.size()) - 1 : 0;
        PlayIndex(prev);
    });

    _idTrackEnded = Event::Register<TrackEndedEvent>([](const TrackEndedEvent&) {
        if (_repeat == RepeatMode::One) { PlayIndex(_currentIndex); return; }
        int32_t next = _currentIndex + 1;
        if (next >= static_cast<int32_t>(_tracks.size())) {
            if (_repeat == RepeatMode::All) next = 0;
            else return; // end of queue
        }
        PlayIndex(next);
    });

    _idStop = Event::Register<StopEvent>([](const StopEvent&) {
        AudioPlayer::Stop();
        _currentIndex = -1;
        BroadcastQueueChanged();
    });

    _idClear = Event::Register<QueueClearEvent>([](const QueueClearEvent&) {
        AudioPlayer::Stop();
        _tracks.clear();
        _currentIndex = -1;
        BroadcastQueueChanged();
    });

    _idRemove = Event::Register<RemoveTrackEvent>([](const RemoveTrackEvent& e) {
        int32_t idx = e.index;
        if (idx < 0 || idx >= static_cast<int32_t>(_tracks.size())) return;
        _tracks.erase(_tracks.begin() + idx);
        if (_currentIndex == idx) {
            // removed currently playing track
            AudioPlayer::Stop();
            _currentIndex = -1;
            if (!_tracks.empty()) PlayIndex(std::min(idx, static_cast<int32_t>(_tracks.size()) - 1));
        } else if (_currentIndex > idx) {
            --_currentIndex;
        }
        BroadcastQueueChanged();
    });

    _idShuffle = Event::Register<ShuffleToggleEvent>([](const ShuffleToggleEvent&) {
        _shuffle = !_shuffle;
        if (_shuffle && !_tracks.empty()) {
            // Move current track to front, shuffle the rest
            std::random_device rd;
            std::mt19937 rng(rd());
            if (_currentIndex >= 0 && _currentIndex < static_cast<int32_t>(_tracks.size())) {
                std::swap(_tracks[0], _tracks[_currentIndex]);
                _currentIndex = 0;
                std::shuffle(_tracks.begin() + 1, _tracks.end(), rng);
            } else {
                std::shuffle(_tracks.begin(), _tracks.end(), rng);
            }
        }
        BroadcastQueueChanged();
    });

    _idRepeat = Event::Register<RepeatToggleEvent>([](const RepeatToggleEvent&) {
        switch (_repeat) {
            case RepeatMode::None: _repeat = RepeatMode::All; break;
            case RepeatMode::All:  _repeat = RepeatMode::One; break;
            case RepeatMode::One:  _repeat = RepeatMode::None; break;
        }
    });
}

void MusicQueue::Shutdown() {
    Event::Unregister<TrackChangedEvent>(s_idTrackChangedMeta);
    Event::Unregister<EnqueueTracksEvent>(_idEnqueue);
    Event::Unregister<PlayTrackEvent>(_idPlayTrack);
    Event::Unregister<SkipNextEvent>(_idSkipNext);
    Event::Unregister<SkipPrevEvent>(_idSkipPrev);
    Event::Unregister<TrackEndedEvent>(_idTrackEnded);
    Event::Unregister<StopEvent>(_idStop);
    Event::Unregister<QueueClearEvent>(_idClear);
    Event::Unregister<RemoveTrackEvent>(_idRemove);
    Event::Unregister<ShuffleToggleEvent>(_idShuffle);
    Event::Unregister<RepeatToggleEvent>(_idRepeat);
    _tracks.clear();
    _currentIndex = -1;
}
