# Host-side verification

Checks that need no ESP32: the firmware's request validation, persistence
ordering, LED output and control wiring, plus property tests for the
hardware-independent engine and a contract test for the browser UI.

Run everything:

```sh
test/host/run_host_tests.sh              # skips what is absent, exits 0/1
test/host/run_host_tests.sh --require-firmware   # required sandbox gate
```

A C++17 compiler is required. Node.js adds the UI contract test. The serial client/firmware integration requires Python 3 and POSIX pseudo-terminals; when the cached snapshot is present, its guard tests also require Python 3 and Git.
ASan/UBSan are used automatically when the toolchain supports them
(`--no-sanitizers` to disable, `--keep` to keep build artifacts).

The project CTest gate includes the host checks. To compile the firmware harness, supply ArduinoJson:

```sh
cmake -S . -B build -DWALLCLOCK_REQUIRE_FIRMWARE_TESTS=ON \
  -DWALLCLOCK_ARDUINOJSON_DIR=/path/to/ArduinoJson/src
cmake --build build
ctest --test-dir build --output-on-failure
```

With `WALLCLOCK_REQUIRE_FIRMWARE_TESTS=ON`, CMake fails configuration if firmware or ArduinoJson is missing rather than silently skipping the harness. Without that option, the harness is optional and CMake reports when it is skipped. The runner with `--require-firmware` fails if the dependency or Node is unavailable; use that required mode for the sandbox gate. When the emulator snapshot exists, the runner also checks it against current inputs; refresh or delete a stale snapshot in the same PR.

## What is here

| File | Purpose |
|---|---|
| `run_host_tests.sh` | Runs native, UI, firmware, serial integration and snapshot checks; aggregates PASS/FAIL/SKIP results |
| `engine_properties.cpp` | Property tests for `include/clock_engine.h`: bounded indices for every supported geometry, monotone dial mapping, distinct minute positions, brightness bounds, half-period complement, phase continuity, saturating blending, validation ranges |
| `firmware_harness.cpp` | Compiles `src/main.cpp` unmodified and drives it: HTTP routes and conditional writes, config round-trip, rejection cases, NVS/LED allocation failures, cold boot, invalid time-zone recovery, SNTP callback, LED cadence and pixel semantics, index bounds, button debounce, serial provisioning |
| `serial_protocol_probe.cpp` | QA's independent host probe for serial aborts, time-zone application, failure invariants and the disabled listener |
| `serial_pty_bridge.cpp`, `serial_e2e_test.py` | Connect the real firmware serial stub to a PTY and exercise the shipped Python client against it, including valid, invalid and time-zone updates |
| `ui_contract_test.mjs` | Executes the real HTML/JS from `include/web_ui.h` under a DOM shim, checks it renders what `/api/config` returns and submits exactly the 13 members `decode()` requires, then hands that body to the firmware harness |
| `stubs/` | Host shims for Arduino, `WebServer`, `Preferences`, `WiFi`, `esp_timer` and `Adafruit_NeoPixel`. Test instrumentation (captured frames, out-of-range writes, forced NVS failures, virtual clock) lives here |
| `CMakeLists.txt` | Optional CTest integration, additive and self-disabling when inputs are missing |

The firmware harness links against the **real ArduinoJson** (pinned to the
version in `platformio.ini`) so JSON typing behaves as it does on the device.
`run_host_tests.sh` clones it into a temporary directory on demand; to use a
local checkout, set `ARDUINOJSON_SRC` (or `WALLCLOCK_ARDUINOJSON_DIR` for
CMake).

## What this cannot tell you

- Nothing about the ESP32 target: no xtensa build, no newlib `TZ` behaviour, no
  real heap, no RMT/WS2812 timing, no Wi-Fi stack, no electrical behaviour.
- Nothing about perceived display quality or measured accuracy/latency.
- It does not replace WC-T-015 or WC-T-016, or any hardware acceptance test.

Frame captures are real frames in GRB order (exactly what a WS2812B receives),
but `show()` returns immediately on the host, so throughput is not modelled.

## When the firmware or engine changes

These are the places to look first, in order:

1. **`scenario_config_api` / `scenario_rejections`** — the JSON document shape
   (13 members) and the accepted ranges. Changing either means changing
   `include/web_ui.h`, `src/main.cpp` and `ui_contract_test.mjs` together.
2. **`scenario_display`** — cadence expectation (at least 40 frames/s), pixel
   semantics and the count range. Pixel expectations are computed from
   `Config`, `scale()`, `mapPosition()` and `hourPosition()` only; if those
   helpers are refactored, this scenario is the only place to update.
3. **`scenario_button` / `scenario_serial`** — debounce timing (100 ms of
   virtual time), the `BUTTON_PIN` macro and the `WIFI <ssid>\t<password>`
   serial command.
4. **`stubs/`** — extend only when the firmware starts using a new library
   member, and mirror the real signature. A stub that is more permissive than
   the real API hides bugs; a stub that is stricter creates false failures.

The shims model SNTP callbacks, wall-clock samples, revisions and NeoPixel allocation/pin behavior only as needed for host scenarios. Their assumptions do not establish real ESP32 behavior.
