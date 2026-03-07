#include "application.h"

int main() {
    SkyDropApp app;
    app.Run(tvk::AppConfig{
        .title    = "Skydrop",
        .width    = 200,
        .height   = 300,
        .resizable = false,
        .enableIdleThrottling = true
    });
    return 0;
}
