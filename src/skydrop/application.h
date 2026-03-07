#pragma once

#include <tinyvk/tinyvk.h>

class SkyDropApp : public tvk::App {
public:
    SkyDropApp() = default;
    ~SkyDropApp() override = default;

protected:
    void OnStart()   override;
    void OnUpdate()  override;
    void OnMenuBar() override;
    void OnUI()      override;

private:
    bool _showStats = true;
};
