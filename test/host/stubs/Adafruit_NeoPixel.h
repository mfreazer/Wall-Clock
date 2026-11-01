// Host-only Adafruit_NeoPixel shim. NOT the real driver.
//
// Models the parts the firmware depends on: the pixel buffer, updateLength()
// reallocation, bounds-checked setPixelColor() and show() as a frame capture.
// Like the real driver with NEO_GRB, setPixelColor(n, r, g, b) stores the
// channels in GRB order in the buffer, so captured frames look exactly like
// what would be clocked out to a WS2812B strip.
#pragma once

#include <Arduino.h>

#define NEO_GRB 0x52
#define NEO_KHZ800 0x0000

class Adafruit_NeoPixel {
 public:
  uint8_t* pixels = nullptr;
  uint16_t numLEDs = 0;
  uint16_t numBytes = 0;
  int pin;
  bool begun = false;

  // Instrumentation for tests.
  static bool failNextAlloc;
  static std::vector<std::string> violations;             // writes outside [0, numLEDs)
  static std::vector<std::vector<uint8_t>> frames;        // one entry per show()
  static long shows;

  Adafruit_NeoPixel(uint16_t n, int p, uint16_t) : pin(p) { updateLength(n); }
  ~Adafruit_NeoPixel() { free(pixels); if(pin >= 0) pinMode(pin, INPUT); }

  void begin() { if (pin >= 0) pinMode(pin, OUTPUT); begun=true; }
  void setPin(int16_t p) { if(begun && pin >= 0) pinMode(pin, INPUT); pin=p; if(begun && pin >= 0) pinMode(pin, OUTPUT); }
  uint8_t* getPixels() const { return pixels; }
  uint16_t numPixels() const { return numLEDs; }

  void updateLength(uint16_t n) {
    free(pixels);
    pixels = nullptr;
    if (failNextAlloc) {
      failNextAlloc = false;
      numLEDs = numBytes = 0;
      return;
    }
    numBytes = n * 3;
    pixels = (uint8_t*)malloc(numBytes ? numBytes : 1);
    if (pixels) {
      memset(pixels, 0, numBytes ? numBytes : 1);
      numLEDs = n;
    } else {
      numLEDs = numBytes = 0;
    }
  }

  void clear() {
    if (pixels) memset(pixels, 0, numBytes);
  }

  void setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
    if (i >= numLEDs) {
      violations.push_back("setPixelColor(" + std::to_string(i) + ") outside 0.." + std::to_string(numLEDs - 1) + ")");
      return;
    }
    pixels[i * 3] = g;
    pixels[i * 3 + 1] = r;
    pixels[i * 3 + 2] = b;
  }

  void show() {
    if (!pixels) return;
    ++shows;
    frames.push_back(std::vector<uint8_t>(pixels, pixels + numBytes));
  }

  // Test helpers (not part of the real API).
  static void reset() {
    violations.clear();
    frames.clear();
    shows = 0;
  }
};
