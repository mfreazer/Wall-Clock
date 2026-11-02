// Host-only WiFi shim. Association is simulated; set WiFi.simulateAssociated()
// from a test to move between WL_IDLE_STATUS and WL_CONNECTED.
#pragma once

#include <Arduino.h>

#define WIFI_STA 1
#define WL_IDLE_STATUS 0
#define WL_CONNECTED 3

class WiFiClass {
 public:
  int state = WL_IDLE_STATUS;
  IPAddress ip;
  int beginCount = 0;
  std::string lastSsid, lastPassword;

  void mode(int) {}
  void setAutoReconnect(bool) {}
  void begin(const char* ssid, const char* password) {
    ++beginCount;
    lastSsid = ssid ? ssid : "";
    lastPassword = password ? password : "";
    state = WL_CONNECTED;
  }
  int status() { return state; }
  IPAddress localIP() { return ip; }
};
extern WiFiClass WiFi;
