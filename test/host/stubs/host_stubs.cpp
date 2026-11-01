// Definitions for the host-only Arduino/ESP32 shims.
//
// Time is fully virtual here: `fakeUs` drives esp_timer_get_time()/millis()/
// delay(), and `fakeEpoch` is what time() returns. That is what makes the
// firmware's 20 ms frame loop and its "plausible epoch" gate testable without
// waiting in real time.
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <arduino_shim.h>

SerialClass Serial;
WiFiClass WiFi;

bool Adafruit_NeoPixel::failNextAlloc = false;
std::vector<std::string> Adafruit_NeoPixel::violations;
std::vector<std::vector<uint8_t>> Adafruit_NeoPixel::frames;
long Adafruit_NeoPixel::shows = 0;

bool Preferences::failWrites = false;

int64_t fakeUs = 1000000;
time_t fakeEpoch = 0;

static int g_millis = 1000;
static int g_pin[64];  // INPUT_PULLUP idle level: HIGH. Initialised below.

static struct PinInit {
  PinInit() {
    for (int& p : g_pin) p = HIGH;
  }
} pinInit;

void pinMode(int, int) {}

int digitalRead(int pin) { return (pin >= 0 && pin < 64) ? g_pin[pin] : HIGH; }

// Test hook: drive a GPIO level (see firmware_harness.cpp).
void hostSetPin(int pin, int level) {
  if (pin >= 0 && pin < 64) g_pin[pin] = level;
}

void delay(unsigned long ms) {
  g_millis += (int)ms;
  fakeUs += (int64_t)ms * 1000;
}

unsigned long millis() { return (unsigned long)g_millis; }

void configTzTime(const char* tz, const char*, const char*, const char*) {
  setenv("TZ", tz ? tz : "", 1);
  tzset();
}

// Overrides libc's time() for the whole harness: the firmware's view of the
// wall clock is whatever the test set in fakeEpoch.
extern "C" time_t time(time_t* out) noexcept {
  if (out) *out = fakeEpoch;
  return fakeEpoch;
}
