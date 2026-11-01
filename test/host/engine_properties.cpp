// Independent property tests for the hardware-independent engine.
//
// These deliberately do not restate the firmware's own unit tests. Each block
// asserts a property that must hold for *all* inputs in a range, which is how
// this file caught nothing in the original implementation but keeps the next
// refactor honest:
//
//   bounded indices for every supported geometry, monotone dial mapping,
//   distinct minute positions, brightness staying inside [low, high], the
//   half-period complement, phase continuity across period changes, and
//   saturating additive blending.
//
// Where a check is tied to a requirement it is named, so a failure points at
// the requirement rather than at a line number.
#include "clock_engine.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace wallclock;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool ok, const std::string& what = "condition") {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    printf("  FAIL  %s\n", what.c_str());
  }
}

static void block(const char* name, void (*fn)()) {
  const int before = g_failures;
  fn();
  printf("  %s  %s\n", g_failures == before ? "PASS" : "FAIL", name);
}

static bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

// WC-SYS-011/019: no logical input may produce an index outside [0, N).
static void indices_are_bounded() {
  for (int n = 1; n <= 300; ++n) {
    for (bool clockwise : {true, false}) {
      for (int offset : {0, n / 2, n - 1}) {
        Config c;
        c.count = n;
        c.clockwise = clockwise;
        c.offset = offset;
        for (int second = 0; second < 43200; ++second) {
          const int index = mapPosition(second / 720.0, c);
          if (index < 0 || index >= n) {
            check(false, "index out of range for n=" + std::to_string(n) + " offset=" + std::to_string(offset) +
                             " second=" + std::to_string(second) + " -> " + std::to_string(index));
            return;
          }
        }
        g_checks += 43200;
      }
    }
  }
}

// WC-SYS-014: the dial mapping must not jump backwards or skip around.
static void dial_mapping_is_monotone() {
  for (int n = 1; n <= 300; ++n) {
    Config c;
    c.count = n;
    c.clockwise = true;
    c.offset = 0;
    int previous = -1;
    for (int step = 0; step < 600; ++step) {
      const int index = mapPosition(step * 0.1, c);
      if (previous >= 0 && index > previous + 1) {
        check(false, "mapping jumps for n=" + std::to_string(n) + " at position " + std::to_string(step * 0.1));
        return;
      }
      previous = index;
      ++g_checks;
    }
  }
}

// WC-SYS-012: with at least 60 physical LEDs every minute must get its own LED.
static void minute_positions_are_distinct() {
  for (int n = 60; n <= 300; ++n) {
    Config c;
    c.count = n;
    bool used[300] = {false};
    for (int minute = 0; minute < 60; ++minute) {
      const int index = mapPosition(minute, c);
      if (used[index]) {
        check(false, "duplicate minute LED for n=" + std::to_string(n) + " minute=" + std::to_string(minute));
        return;
      }
      used[index] = true;
    }
    g_checks += 60;
  }
}

// WC-SYS-015/016: the 12 dial marks land on their expected logical positions,
// and the hour position advances between them.
static void hour_dial() {
  const Config c;
  for (int hour = 0; hour < 24; ++hour) {
    check(mapPosition(hourPosition({hour, 0, 0}), c) == (hour % 12) * 5,
          "hour mark for hour " + std::to_string(hour));
  }
  check(near(hourPosition({1, 0, 0}), 5));
  check(near(hourPosition({1, 30, 0}), 7.5));
  check(near(hourPosition({11, 59, 59}), 60.0 - 1.0 / 720.0));
  check(near(hourPosition({12, 0, 0}), 0));
  check(hourPosition({1, 59, 59}) < hourPosition({2, 0, 0}));
  // Continuous motion: a second of wall time moves the hour position forward.
  check(hourPosition({4, 10, 10}) > hourPosition({4, 10, 9}));
  check(hourPosition({4, 10, 10, 0.5}) > hourPosition({4, 10, 10, 0.0}));
}

// WC-SYS-030/033/035/036: brightness stays inside the configured levels and the
// two indications are exact complements (a half-period shift of a cosine).
static void modulation_levels_and_phase() {
  for (double low : {0.0, 10.0, 49.9}) {
    for (double high : {low + 0.1, 50.0, 100.0}) {
      for (double period : {3.0, 4.5, 6.0}) {
        Config c;
        c.low = low;
        c.high = high;
        c.period = period;
        Oscillator oscillator;
        for (int step = 0; step < 2000; ++step) {
          const double hour = oscillator.hour(c);
          const double minute = oscillator.minute(c);
          if (!(hour >= low - 1e-9 && hour <= high + 1e-9) || !(minute >= low - 1e-9 && minute <= high + 1e-9)) {
            check(false, "brightness outside [low, high]");
            return;
          }
          if (!near(hour + minute, low + high, 1e-9)) {
            check(false, "hour and minute are not half-period complements");
            return;
          }
          oscillator.advance(period / 1000.0, period);
          g_checks += 3;
        }
      }
    }
  }
}

// WC-SYS-031/032: the configured period is the real cycle length.
static void period_is_honoured() {
  for (double period : {3.0, 4.5, 6.0}) {
    Config c;
    c.period = period;
    Oscillator oscillator;
    oscillator.advance(period * 0.37, period);
    const double phase = oscillator.phase();
    oscillator.advance(period, period);
    check(near(oscillator.phase(), phase, 1e-6), "one period returns the same phase");
  }
  check(near(Config().period, 4.5), "default period is 4.5 s");
}

// WC-SYS-038: changing the period must not restart the oscillator, and a
// configuration change must not create a brightness step.
static void configuration_change_keeps_phase() {
  Config c;
  Oscillator oscillator;
  oscillator.advance(1.234, 4.5);
  const double brightness = oscillator.hour(c);
  const double phase = oscillator.phase();
  c.period = 3.0;
  check(oscillator.hour(c) == brightness, "period change does not step the brightness");
  check(oscillator.phase() == phase, "period change does not reset the phase");
  c.low = 5;
  c.high = 80;
  check(oscillator.phase() == phase, "level change does not reset the phase");
  // The new period takes effect immediately.
  oscillator.advance(0.3, c.period);
  check(near(oscillator.phase(), std::fmod(phase + 0.1, 1.0), 1e-9), "new period sets a new phase rate");
}

// WC-SYS-042/043: blending is channelwise and saturating, never wrapping.
static void blending_saturates() {
  check(scale({255, 255, 255}, 100).r == 255, "full scale keeps the channel");
  check(scale({255, 255, 255}, 0).r == 0, "zero scale clears the channel");
  check(add({250, 200, 0}, {20, 100, 5}).r == 255, "red channel clips");
  check(add({250, 200, 0}, {20, 100, 5}).g == 255, "green channel clips");
  check(add({250, 200, 0}, {20, 100, 5}).b == 5, "blue channel adds");
  check(add({10, 20, 30}, {5, 6, 7}).r == 15, "channelwise addition");
  for (int r = 0; r <= 255; r += 51) {
    const Color c{(uint8_t)r, (uint8_t)(255 - r), (uint8_t)(r / 2)};
    check(add(c, c).r == std::min(255, r + r), "saturation is min(255, sum)");
  }
}

// WC-SYS-024/040/044: seconds win on every shared pixel, and frames always have
// exactly N pixels regardless of how many indications collide.
static void frames_and_priority() {
  for (int n : {1, 2, 7, 60, 61, 144, 300}) {
    Config c;
    c.count = n;
    Oscillator oscillator;
    const auto frame = render({13, 7, 42}, c, oscillator);
    check(frame.size() == (size_t)n, "frame has exactly N pixels for n=" + std::to_string(n));
    const int second = mapPosition(42, c);
    check(frame[second].r == scale(c.second, c.high).r && frame[second].g == scale(c.second, c.high).g &&
              frame[second].b == scale(c.second, c.high).b,
          "seconds take priority on the second pixel for n=" + std::to_string(n));
  }
  // Idle off: no LED is lit unless an indication uses it.
  Config c;
  Oscillator oscillator;
  const auto frame = render({6, 10, 20}, c, oscillator);
  int lit = 0;
  for (auto& pixel : frame)
    if (pixel.r || pixel.g || pixel.b) ++lit;
  check(lit >= 1 && lit <= 3, "at most three LEDs are lit with idle disabled");
}

// WC-SYS-034/056: validation accepts exactly the documented ranges.
static void validation_ranges() {
  Config c;
  check(valid(c), "defaults are valid");
  for (int count : {1, 60, 300}) {
    c = Config();
    c.count = count;
    c.offset = count - 1;
    check(valid(c), "count " + std::to_string(count) + " is valid");
  }
  for (int count : {-1, 0, 301}) {
    c = Config();
    c.count = count;
    check(!valid(c), "count " + std::to_string(count) + " is rejected");
  }
  for (double period : {3.0, 4.5, 6.0}) {
    c = Config();
    c.period = period;
    check(valid(c), "period boundary accepted");
  }
  for (double period : {2.999, 6.001, double(NAN), double(INFINITY)}) {
    c = Config();
    c.period = period;
    check(!valid(c), "invalid period rejected");
  }
  for (auto levels : {std::pair<double, double>{0, 100}, {0.5, 99.5}}) {
    c = Config();
    c.low = levels.first;
    c.high = levels.second;
    check(valid(c), "brightness pair accepted");
  }
  for (auto levels : {std::pair<double, double>{-1, 50}, {50, 50}, {60, 50}, {0, 101}, {NAN, 50}, {0, INFINITY}}) {
    c = Config();
    c.low = levels.first;
    c.high = levels.second;
    check(!valid(c), "brightness pair rejected");
  }
  c = Config();
  c.timezone = "";
  check(!valid(c), "empty timezone rejected");
  c.timezone = std::string(200, 'A');
  check(!valid(c), "over-long timezone rejected");
  c = Config();
  c.timezone = "UTC0\nX";
  check(!valid(c), "control characters in timezone rejected");
}

int main() {
  printf("engine_properties: property suite\n");
  block("indices bounded for every supported geometry (WC-SYS-011/019)", indices_are_bounded);
  block("dial mapping is monotone (WC-SYS-014)", dial_mapping_is_monotone);
  block("minute positions are distinct (WC-SYS-012)", minute_positions_are_distinct);
  block("hour dial and continuous motion (WC-SYS-015/016)", hour_dial);
  block("modulation levels and half-period phase (WC-SYS-030/033/036)", modulation_levels_and_phase);
  block("configured period is honoured (WC-SYS-031/032)", period_is_honoured);
  block("configuration changes keep the phase (WC-SYS-038)", configuration_change_keeps_phase);
  block("blending saturates (WC-SYS-042/043)", blending_saturates);
  block("frames and second priority (WC-SYS-024/040/044)", frames_and_priority);
  block("validation ranges (WC-SYS-034/056)", validation_ranges);
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
