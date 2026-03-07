#include "event.h"

Event* Event::_instance = nullptr;

int Event::Init() {
    if (_instance) return -1;
    _instance = new Event();
    return 0;
}

void Event::Shutdown() {
    delete _instance;
    _instance = nullptr;
}