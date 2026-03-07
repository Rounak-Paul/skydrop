#pragma once

#include <tinyvk/tinyvk.h>
#include "event.h"

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
    ListenerID _quitID   = 0;
    ListenerID _volumeID = 0;
    ListenerID _pauseID  = 0;
    ListenerID _seekID   = 0;
};
