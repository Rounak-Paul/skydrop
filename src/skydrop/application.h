#pragma once

#include <tinyvk/tinyvk.h>
#include "event.h"
#include "session.h"
#include <cstdint>
#include <string>
#include <vector>

class SkyDropApp : public tvk::App {
public:
    SkyDropApp() = default;
    ~SkyDropApp() override = default;

protected:
    void OnStart()   override;
    void OnStop()    override;
    void OnUpdate()  override;
    void OnMenuBar() override;
    void OnUI()      override;

private:
    // Register OS-level drag-drop on the GLFW window
    void RegisterDropCallback();
    // Save current session to disk
    void SaveSession();

    ListenerID _quitID          = 0;
    ListenerID _volumeID        = 0;
    ListenerID _pauseID         = 0;
    ListenerID _seekID          = 0;
    ListenerID _queueChangedID  = 0;

    // Cached state for session save
    float       _currentVolume    = 1.0f;
    float       _currentPos       = 0.0f;

    // Cached queue snapshot for session save
    std::vector<std::string> _sessionQueuePaths;
    int32_t                  _sessionCurrentIndex = -1;


};
