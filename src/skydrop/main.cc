#include "application.h"

int main() {
    SkyDropApp app;
    app.Run(tvk::AppConfig{
        .title    = "Skydrop",
        .width    = 280,
        .height   = 420,
        .resizable = true,
        .enableIdleThrottling = true
    });
    return 0;
}
