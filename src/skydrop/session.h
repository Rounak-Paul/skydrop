#pragma once

#include <string>
#include <vector>

// Persists playback session to ~/.skydrop/session.json so the app
// restores its last state (queue, position, volume, shuffle, repeat)
// on next launch.

struct SessionState {
    std::vector<std::string> queuePaths;
    int32_t currentIndex    = -1;
    float   posSeconds      = 0.0f;
    float   volume          = 1.0f;
    bool    shuffle         = false;
    int     repeatMode      = 0; // 0=None 1=One 2=All
};

class Session {
public:
    static void Save(const SessionState& s);
    static bool Load(SessionState& out);   // returns false if no file

private:
    static std::string GetPath();
};
