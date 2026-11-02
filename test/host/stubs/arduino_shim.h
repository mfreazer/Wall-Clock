// Host-only declarations for Arduino-ESP32 core functions that src/main.cpp
// uses but that no individual library header provides.
#pragma once

#include <ctime>

// time.h from the ESP32 core: applies a POSIX TZ string and starts SNTP.
void configTzTime(const char* tz, const char* server1, const char* server2 = nullptr, const char* server3 = nullptr);
