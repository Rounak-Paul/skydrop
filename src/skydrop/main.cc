#include "application.h"

int main() {
    SkyDropApp app;
    app.Run(tvk::AppConfig{
        .title  = "Skydrop",
        .width  = 200,
        .height = 300,
        .enableIdleThrottling = true
    });
    return 0;
}
