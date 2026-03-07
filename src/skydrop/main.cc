#include "application.h"

int main() {
    SkyDropApp app;
    app.Run(tvk::AppConfig{
        .title  = "Skydrop",
        .width  = 300,
        .height = 500,
    });
    return 0;
}
