ESP32 LED Ring Wall Clock

A wall clock built around an ESP32 development board and an addressable LED strip arranged in a ring.

The clock represents hours, minutes, and seconds using colored light. Hours and minutes use smooth, phase-shifted brightness modulation; seconds remain at constant high brightness. When indications collide, seconds take priority, while hour and minute colors blend additively when they share an LED without a second indication.

Project status

Phase: Development implementation
Implementation: Portable C++ clock engine, ESP32 Arduino firmware, local configuration UI, and native engine tests. The unauthenticated web listener is temporarily disabled while secure access is developed; browser configuration is unavailable. Hardware acceptance is pending.

The system requirements are documented in [docs/SyRS.md](docs/SyRS.md). Provisional development decisions, defaults, limitations and verification status are recorded in [docs/Implementation.md](docs/Implementation.md); they are not silently promoted to approved requirements.

Concept

Time indications

- Hours: A traditional 12-position dial. The hour position advances continuously as time progresses.
- Minutes: One of 60 logical positions, corresponding to the current minute value.
- Seconds: One of 60 logical positions, corresponding to the current second value.

The number of physical LEDs is not fixed at 60. The mapping from logical time positions to physical LED indices remains to be defined.

Colors

The colors of hour, minute, and second indications are configurable. The hour indication supports separate AM and PM colors, allowing the two halves of the day to be distinguished without changing the 12-position dial.

Unassigned LEDs are off by default. Users can enable idle illumination, with white as the initial default idle color.

Brightness modulation

Hours and minutes use the same low and high brightness levels, with a smooth cosine waveform.

The modulation period is configurable from 3 to 6 seconds, with a default of 4.5 seconds. The minute indication is shifted by half a period relative to the hour indication.

The modulation runs continuously and independently of changes to the displayed time.

Seconds use constant high brightness.

Collision handling

The display uses the following rules:

1. Seconds override everything else. If a second indication shares a physical LED with an hour or minute indication, only the second color is displayed at high brightness.
2. Hours and minutes blend additively. If they share an LED and no second indication is present, their modulated RGB contributions are added channel by channel.
3. Unassigned LEDs remain off unless idle illumination is enabled.

Each RGB output channel is limited to the range supported by the selected LED hardware.

Configuration

The planned configurable parameters include:

Parameter| Initial value or constraint
Physical LED count| To be selected
Logical minute/second positions| 60
Hour dial| 12 positions
Hour AM color| To be defined
Hour PM color| To be defined
Minute color| To be defined
Second color| To be defined
Idle illumination| Disabled
Idle color| White
Modulation period| 4.5 seconds
Modulation period range| 3–6 seconds
Low brightness| To be defined
High brightness| To be defined
Time source| Network time over Wi-Fi
Time zone| To be defined

Valid configuration shall persist across power cycles. The precise configuration mechanism and defaults remain subject to the SyRS.

User interaction

The system shall provide both physical controls and a web interface.

The precise functions assigned to each interface remain to be defined. The web interface is intended for local interaction and configuration; its access policy and authentication requirements are open decisions.

Hardware

The initial hardware concept consists of:

- An ESP32 development board.
- An addressable RGB or RGBW LED strip suitable for circular installation.
- A suitable LED power supply.
- Physical controls.
- A Wi-Fi network for time synchronization.

The exact board, LED type, LED count, power supply, GPIO assignments, and electrical interfaces have not yet been selected.

The implementation must account for LED current requirements, logic-level compatibility, grounding, and the ESP32 board's electrical limits.

Software design principles

The implementation should preserve the separation between:

1. Timekeeping: obtains synchronized time and derives local hour, minute, and second values.
2. Logical display model: calculates the intended hour, minute, and second positions.
3. Physical mapping: converts logical positions into physical LED indices.
4. Modulation: calculates continuous hour and minute brightness values.
5. Rendering: resolves collisions, computes final RGB values, and updates the LED strip.
6. Configuration: validates, applies, and persists user settings.
7. User interfaces: handles physical controls and web interactions.

This separation allows the logical display and collision behavior to be tested independently of the selected LED hardware.

The modulation timebase must remain independent of wall-clock synchronization and LED refresh operations.

Testing

The project should include automated tests for:

- Logical time-to-position mapping.
- Hour movement between hour marks.
- LED index bounds.
- Modulation waveform and period limits.
- Half-period phase offset.
- Modulation continuity when displayed time changes.
- Seconds-over-hours/minutes priority.
- Additive RGB blending and output limiting.
- AM/PM color selection.
- Configuration validation and persistence.
- Recovery from network and power interruptions.
- Physical-control and web-interface behavior.

Hardware integration tests will be required to verify actual LED rendering, electrical operation, and perceived display behavior.

For the registry-blocked review sandbox, `./tools/sandbox_esp32_build.sh --emulate` builds the ESP32 target and runs a limited boot/serial smoke test. **Its third-party compiler is not provenance-verified; do not flash or release its output.** See [docs/Sandbox-ESP32.md](docs/Sandbox-ESP32.md) for requirements, provenance, and limitations. For an unchanged source snapshot, `./tools/sandbox_esp32_cached_smoke.sh` reuses the Git-stored emulator-only image without rebuilding; it checks for stale inputs first.

Documentation

- "README.md" — project overview, behavior, configuration, architecture, and development status.
- "SyRS.md" — normative system requirements and verification objectives.

Additional architecture, software requirements, and test specifications should be created as implementation decisions become stable.

Current open decisions

Before implementation acceptance criteria can be finalized, the project needs decisions on:

1. Mapping 60 logical positions to the selected physical LED count.
2. Ring orientation and physical index direction.
3. Hour-position quantization.
4. Timekeeping behavior during Wi-Fi loss and power interruption.
5. Time-zone configuration.
6. Physical-control and web-interface functions.
7. LED hardware and power supply.
8. Default brightness levels.
9. Required time accuracy and display update latency.

These decisions should be reflected in "SyRS.md" before they are treated as implementation constraints.


## Build and run

Install [PlatformIO](https://platformio.org/) and run:

```sh
pio run -e esp32
pio run -e esp32 -t upload
pio device monitor -b 115200
```

Read the hardware and safety profile in [docs/Implementation.md](docs/Implementation.md) before connecting a strip. The default data pin is GPIO18, and the optional active-low button is GPIO27 to ground.

At the serial prompt send `WIFI <ssid><TAB><password>` followed by Enter (replace `<TAB>` with a literal tab). Credentials persist in NVS. After joining the network, the clock prints its IP every 30 seconds. **The web interface is temporarily disabled**: no HTTP listener is started and settings cannot currently be changed from a browser. On a POSIX computer, settings can be read and updated over USB instead:

```sh
python3 tools/serial_config.py /dev/ttyUSB0 get > clock-settings.json
# Edit the JSON, then send the complete document back:
python3 tools/serial_config.py /dev/ttyUSB0 set clock-settings.json
```

Use the actual serial device path. The tool does not require third-party Python packages. The clock validates the entire configuration and reports errors without applying invalid or incomplete updates; a concurrent button change rejects a stale update. The serial path is not yet verified on physical hardware. Before network time is available, the LEDs remain off. There is no setup access point or cloud dependency beyond network time.

## Native tests (no ESP32 needed)

```sh
# Install Node.js to include the embedded web UI test in CTest.
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or, with only a C++17 compiler:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude test/engine_test.cpp -o /tmp/wallclock-engine
/tmp/wallclock-engine
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude test/requirements_test.cpp -o /tmp/wallclock-requirements
/tmp/wallclock-requirements
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude test/timezone_test.cpp -o /tmp/wallclock-timezone
/tmp/wallclock-timezone
```

The basic CMake command above skips the firmware harness unless an ArduinoJson path is supplied; it is not the full gate. For the full QA suite, run `test/host/run_host_tests.sh --require-firmware`; see [test/host/README.md](test/host/README.md) for dependencies and CTest integration. With Node.js installed, CTest also runs `test/web_ui_test.js`. If Node.js is missing, CMake reports that the UI test was not registered. The C++ and UI tests are host-only and are not run by `pio test -e esp32`.

Source layout: `include/clock_engine.h` contains hardware-independent geometry, modulation, validation and rendering; `src/main.cpp` handles SNTP, NVS, HTTP, controls and NeoPixel output; `include/web_ui.h` is the self-contained local UI. No credentials should be committed.

Review findings and outstanding risks are recorded in [docs/Review.md](docs/Review.md). The open access-policy decisions and release/hardware verification plan are in [docs/Release-Readiness.md](docs/Release-Readiness.md).
