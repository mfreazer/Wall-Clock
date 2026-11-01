// Host-only Arduino shim. NOT the real Arduino core.
//
// Purpose: let src/main.cpp compile and run on a desktop so the firmware's
// HTTP/NVS/button/render wiring can be exercised without an ESP32.
// Only the surface used by the firmware is implemented; behaviour that
// matters to the tests (String semantics, millis, digitalRead) is modelled
// faithfully, everything else is inert.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <time.h>
#include <vector>

#define PROGMEM
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

typedef uint8_t byte;

// Arduino String, reduced to the members the firmware and ArduinoJson use.
// `rd` models String's internal read cursor so ArduinoJson can deserialize
// directly from a String, as it does on the device.
class String {
 public:
  std::string s;

  String() {}
  String(const char* p) : s(p ? p : "") {}
  String(const std::string& p) : s(p) {}
  explicit String(char c) : s(1, c) {}

  const char* c_str() const { return s.c_str(); }
  unsigned int length() const { return (unsigned int)s.size(); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }

  int indexOf(char c, unsigned int from = 0) const {
    size_t i = s.find(c, from);
    return i == std::string::npos ? -1 : (int)i;
  }

  String substring(unsigned int a) const { return String(s.substr(a)); }
  String substring(unsigned int a, unsigned int b) const { return String(s.substr(a, b - a)); }

  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator=(const char* p) { s = p ? p : ""; return *this; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator==(const char* p) const { return s == (p ? p : ""); }
  bool operator!=(const char* p) const { return !(*this == p); }
  explicit operator bool() const { return !s.empty(); }

  // ArduinoJson writer/reader hooks.
  size_t write(const uint8_t* buf, size_t n) { s.append((const char*)buf, n); return n; }
  size_t write(uint8_t c) { s += (char)c; return 1; }
  int read() const { return rd < s.size() ? (unsigned char)s[rd++] : -1; }
  int available() const { return (int)(s.size() - rd); }

 private:
  mutable size_t rd = 0;
};

class IPAddress {
 public:
  uint32_t value = 0;
  std::string toStdString() const {
    char b[32];
    snprintf(b, sizeof b, "%u.%u.%u.%u", (unsigned)(value >> 24) & 255, (unsigned)(value >> 16) & 255,
             (unsigned)(value >> 8) & 255, (unsigned)value & 255);
    return b;
  }
};

class SerialClass {
 public:
  std::string out;        // everything the firmware printed
  std::vector<char> in;   // bytes the test feeds to the firmware
  void begin(unsigned long) {}
  int available() { return (int)in.size(); }
  int read() {
    if (in.empty()) return -1;
    char c = in.front();
    in.erase(in.begin());
    return c;
  }
  void print(const char* p) { out += p ? p : ""; }
  void print(const String& p) { out += p.s; }
  void print(const std::string& p) { out += p; }
  void print(int v) { out += std::to_string(v); }
  void print(unsigned int v) { out += std::to_string(v); }
  void print(const IPAddress& ip) { out += ip.toStdString(); }
  void println() { out += "\n"; }
  template <class T>
  void println(const T& v) { print(v); println(); }
};
extern SerialClass Serial;

void pinMode(int pin, int mode);
int digitalRead(int pin);
void delay(unsigned long ms);
unsigned long millis();

// Declarations for Arduino-ESP32 core functions (configTzTime, ...).
#include "arduino_shim.h"
