# SyRS → implementation → verification traceability

## Baseline and scope

Source: [SyRS.md](SyRS.md), WC-SyRS 1.0-draft. Requirement text and acceptance criteria are **unchanged**. This matrix audits the current development implementation; it is not a compliance certificate. [Implementation.md](Implementation.md) contains **unapproved provisional choices**, not amendments to the SyRS. Passing tests of those choices does not approve them.

Evidence references use file + symbol rather than unstable line numbers:

- **E**: [`include/clock_engine.h`](../include/clock_engine.h): `Config`, `valid`, `hourPosition`, `mapPosition`, `Oscillator`, `scale`, `add`, `render`.
- **F**: [`src/main.cpp`](../src/main.cpp): `encode`, `decode`, `save`, `applyStripConfiguration`, `setup`, `loop`; routes identified by path/method.
- **U**: [`include/web_ui.h`](../include/web_ui.h): `WEB_UI`, `refresh`, `poll`, form submission.
- **R**: [`test/requirements_test.cpp`](../test/requirements_test.cpp): named test functions below. **These are executable checks, not planned tests.**
- **L**: [`test/engine_test.cpp`](../test/engine_test.cpp): aggregate smoke test. Both L and R use always-active checks, including Release builds.

- **H**: [`test/host`](../test/host/README.md): QA property checks, stubbed firmware scenarios with real ArduinoJson, and Node UI contract. These execute the firmware and browser script on a host, not on an ESP32 or in a real browser.

Statuses:

- **Core tested**: passing native checks for the stated software behavior only; not ESP32/system acceptance.
- **Partial**: some implementation/evidence exists, but important verification is missing.
- **Blocked**: acceptance depends on an unresolved SyRS decision, even if provisional code exists.
- **Unverified**: no executed verification establishes the requirement.

Multiple statuses may apply. No requirement is claimed system-accepted. No line/branch coverage percentage has been measured.

## Requirement matrix (all 55 WC-SYS requirements)

| SyRS ID | Implementation evidence | Executed test evidence | Status / limitation |
|---|---|---|---|
| WC-SYS-001 | F `setup`: WiFi + `configTzTime` | None | Partial: no ESP32 build or network test |
| WC-SYS-002 | F `loop`: epoch → `localtime_r` → E `render` | R `positions` tests supplied times only | Partial: real time derivation untested |
| WC-SYS-003 | E `Config.timezone`; F `applyStripConfiguration`, `setup`; U zone field | R `validation`; `test/timezone_test.cpp`; H invalid stored recovery | Blocked: supported TZ subset unapproved; representative DST cases checked on host libc and ESP32/newlib in emulation, not physical hardware |
| WC-SYS-004 | F GET `/api/status`; U `poll` | H `scenario_sync_status` | Blocked: callback confirms a sync since boot but not its recency; indication TBD; target callback integration untested |
| WC-SYS-005 | F reconnect + system-clock holdover | None | Blocked: outage policy unapproved and untested |
| WC-SYS-006 | F `setup`, `loop`: boot dark pending SNTP callback and plausible epoch | None | Blocked: restart policy unapproved and untested |
| WC-SYS-010 | F NeoPixel driver only | None | Unverified: physical ring installation required |
| WC-SYS-011 | E `Config.count`, `valid`, `render`; F `applyStripConfiguration` | R `positions`, `validation` | Core tested for sampled counts; hardware allocation/update untested; 1–300 provisional |
| WC-SYS-012 | E `render`: minute → `mapPosition` | R `positions` (0,59) | Core tested endpoints; firmware source values untested |
| WC-SYS-013 | E `render`: second → `mapPosition` | R `positions` (0,59) | Core tested endpoints; firmware source values untested |
| WC-SYS-014 | E `mapPosition` | R `positions` | Blocked: nearest-pixel mapping unapproved; tests characterize prototype |
| WC-SYS-015 | E `hourPosition`: hour modulo 12 | R `positions` | Core tested selected marks/wrap, not all twelve marks |
| WC-SYS-016 | E `hourPosition` includes minutes/seconds/fraction | R `positions` | Core tested progression and fractional input; firmware subsecond integration untested |
| WC-SYS-017 | E `mapPosition` rounding | R `positions`: half-up example | Blocked: quantization unapproved |
| WC-SYS-018 | E offset/direction; F `applyStripConfiguration`; U controls | R `positions` | Blocked: orientation unapproved; physical installation untested |
| WC-SYS-019 | E mapping; F bounded LED loop | R `positions`: all whole-second hour positions for five counts, both directions, offset N−1 | Core tested mapping bounds; actual LED writes and all offsets/counts not exhaustively instrumented |
| WC-SYS-020 | E colors; F encode/decode; U color fields | R `collisions`: selected colors | Partial: H exercises the API with stubs; target interface untested |
| WC-SYS-021 | E `Config.am`; F encode/decode; U | R `collisions` | Partial: AM rendering core tested and H exercises the API; target path untested |
| WC-SYS-022 | E `Config.pm`; F encode/decode; U | R `collisions` | Partial: PM rendering core tested and H exercises the API; same AM/PM values permitted; target path untested |
| WC-SYS-023 | E `render`: hour < 12 | R `collisions`: before noon, noon, midnight | Core tested; local-time conversion not covered |
| WC-SYS-024 | E `render` frame initialization | R `collisions` | Core tested idle off |
| WC-SYS-025 | E `Config.idle`, `render`; F button/API; U | R `collisions` | Partial: core toggle and H button/API paths tested; target controls untested |
| WC-SYS-026 | E `Config.background`, `render`; F/U config | R `collisions` | Partial: white default/custom idle and H API paths tested; target persistence untested; idle brightness uses low level as an unapproved provisional choice |
| WC-SYS-030 | E `Oscillator.hour`, `.minute` | R `modulation` | Core tested sampled waveform; visual smoothness unverified |
| WC-SYS-031 | E `valid`; F decode; U range | R `validation`, `modulation` | Core endpoints/out-of-range and H API rejection tested; target API untested |
| WC-SYS-032 | E `Config.period` | R `modulation` | Core tested 4.5-second default |
| WC-SYS-033 | E shared `low`/`high`, `Oscillator` | R `modulation` | Core tested shared levels |
| WC-SYS-034 | E `valid` | R `validation` | Core tested endpoints, equality, reversal, nonfinite and out-of-range values |
| WC-SYS-035 | E `Oscillator` | R `modulation`: 101 samples per period at 3/4.5/6 seconds | Core tested cosine/phase relationship; physical waveform unverified |
| WC-SYS-036 | E `Oscillator.minute` complement | R `modulation` | Core tested half-period relationship |
| WC-SYS-037 | F monotonic clock; E oscillator independent of `Time` | R `modulation`: render different times without phase change | Partial: core separation tested; SNTP correction integration untested |
| WC-SYS-038 | E integrated phase; F `applyStripConfiguration` retains oscillator | R `modulation`: core config mutation/rate | Partial: H exercises configuration/button paths; target integration untested; immediate brightness changes may jump output |
| WC-SYS-039 | E `render` second uses `high` | R `modulation`, `collisions` | Core tested across phase samples |
| WC-SYS-040 | E `render` second write last | R `collisions`: triple, hour-second, minute-second | Core tested at all 60 offsets for N=60 |
| WC-SYS-041 | E `render`: shared hour/minute calls `add` | R `collisions`; L non-saturated blend | Core tested selected overlaps |
| WC-SYS-042 | E `scale`, `add` | R `collisions`; L channelwise addition | Core tested selected values; rounds contributions before addition (review quantization intent) |
| WC-SYS-043 | E `add` saturation, 8-bit `Color` | R `collisions`; L | Core tested clipping; RGB hardware profile only |
| WC-SYS-044 | E index-independent collision logic | R `collisions` at all 60 offsets | Core tested N=60; not every count/geometry combination |
| WC-SYS-050 | F `setup` input pullup; `loop` debounce | H `scenario_button` | Blocked: button function unapproved; real GPIO untested |
| WC-SYS-051 | F routes; U web form | H `scenario_config_api`, `ui_contract` | Partial: mocked HTTP and DOM only; no browser/ESP32 execution |
| WC-SYS-052 | F/U controls | None | Blocked: interface functions unapproved |
| WC-SYS-053 | F decode/API; U numerical fields; E `valid` | R `validation` | Partial: native rules and H mocked interaction tested; target interaction untested |
| WC-SYS-054 | F encode/decode/API; U five colors | R `collisions` tests selected rendered colors only | Partial: H tests malformed RGB/API and UI contract; target workflow untested |
| WC-SYS-055 | F API/button; U idle checkbox | R `collisions` tests core idle only | Partial: H tests stubbed button/API and UI contract; target interfaces untested |
| WC-SYS-056 | F decode + save-before-apply; E `valid` | R `validation`; H `scenario_rejections` | Partial: host rejection and state invariance checked; representative target newlib cases pass in emulation, not exhaustive or physical hardware |
| WC-SYS-057 | F `save`, boot `decode`, NVS | H `scenario_persistence`, `scenario_rejections` | Partial: simulated storage, reboot and write failure tested; real power cycles untested |
| WC-SYS-058 | F HTTP no auth; JSON writes; U | None | Blocked: access policy unapproved; JSON/no CORS is not LAN authentication |
| WC-SYS-060 | F `loop` 20ms frame target and `strip.show` | R `modulation` tests frame values only | Partial: physical refresh timing untested |
| WC-SYS-061 | F `esp_timer_get_time` vs `time`; E oscillator | R `modulation` | Partial: core independence tested, firmware clock correction untested |
| WC-SYS-062 | F loop/apply retain oscillator; E `render` does not mutate it | R `modulation` | Partial: core render continuity tested, driver integration untested |
| WC-SYS-063 | F nominal 20ms target | None | Blocked: latency bound TBD; HTTP/serial/LED operations can delay output |
| WC-SYS-064 | F SNTP/system clock | None | Blocked: accuracy conditions/limits TBD |
| WC-SYS-070 | E `scale`, `add`, `render` | R `modulation`, `collisions` | Partial / interpretation needed: each contribution obeys levels, additive output can exceed B_H; idle brightness uses low as a provisional choice; no aggregate brightness/current limiter |
| WC-SYS-071 | Only provisional wiring guidance in Implementation.md | None | Unverified: no electrical/current/thermal measurements |
| WC-SYS-072 | Only provisional supply guidance in Implementation.md | None | Blocked: hardware/supply not approved or verified |

## SyRS verification objectives → executable checks

| SyRS test | Current evidence in R | Coverage boundary / missing evidence |
|---|---|---|
| WC-T-001 | `positions`: minute 0 and 59 at N=60 | Native only |
| WC-T-002 | `positions`: second 0 and 59 at N=60 | Native only |
| WC-T-003 | `positions`: half-hour, hour boundary and dial wrap | Selected times and fractional core input; firmware time input untested |
| WC-T-004 | `positions`: endpoints, bounds for five counts, two directions; reference offset; rounding example | Provisional mapping only; acceptance blocked by TBD |
| WC-T-005 | `modulation`: sampled cosine including low, high, boundary | Native numeric tolerance 1e-8 |
| WC-T-006 | `modulation`: 3, 4.5, 6 seconds | Native oscillator only |
| WC-T-007 | `modulation`: hour+minute equals low+high throughout samples | Native only |
| WC-T-008 | `modulation`: changing supplied display time retains phase | No actual SNTP correction test |
| WC-T-009 | `collisions`: all collision types at 60 offsets | N=60 only |
| WC-T-010 | `collisions`: saturated blend; L: non-saturated blend | Selected RGB values |
| WC-T-011 | `collisions`: all-channel saturation; L: channelwise saturation | RGB8 profile only |
| WC-T-012 | `collisions`: 11:59:58, 12:00:01, 00:01:02 | Selection near boundaries; Montréal DST tested on host libc and ESP32/newlib in emulation, not physical hardware |
| WC-T-013 | `collisions`: idle off, default white, custom idle on | Core only |
| WC-T-014 | R `validation`; H JSON rejection, state invariance, simulated save/reload, corrupt NVS and timezone recovery | **Partial**: target NVS, persistence failures and power loss untested |
| WC-T-015 | None | **Not run; blocked** pending approved outage/restart policy and ESP32/network fixture |
| WC-T-016 | None | **Not run; blocked** pending approved interaction specification and GPIO/browser fixtures |

## Reproduce the executed evidence

From the repository root, with a C++17 compiler:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude test/requirements_test.cpp -o /tmp/wallclock-requirements
/tmp/wallclock-requirements
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude test/engine_test.cpp -o /tmp/wallclock-engine
/tmp/wallclock-engine
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude test/timezone_test.cpp -o /tmp/wallclock-timezone
/tmp/wallclock-timezone
```

All three binaries pass via CMake/CTest in Debug and Release in the review sandbox. R emits five named group results; individual checks fail with descriptive messages and a nonzero exit code. A passing group is not a count of separately implemented SyRS tests. CMake registers L, R, the time-zone tests and the available H tests. The required host runner passed against the revision with real ArduinoJson and stubs; see H for coverage and limitations. The ESP32 target also compiled and passed a limited emulator boot and serial validation smoke test using an unverified third-party compiler; see [Sandbox-ESP32.md](Sandbox-ESP32.md). This is not an official-toolchain verification or hardware acceptance. No target HTTP, browser, real NVS, Wi-Fi, hardware, accuracy, latency or power-interruption tests were executed.

## Acceptance gaps and change discipline

1. Owner approval is required for every open decision in SyRS section 6. This audit does not approve or rewrite any of them.
2. Validate the firmware toolchain/dependencies before claiming buildability. Extend the existing host fixtures and validate JSON/API, persistence, GPIO debounce and networking on target.
3. Define measurable hardware accuracy, latency and electrical acceptance criteria before conducting system tests.
4. Resolve the WC-SYS-070 additive-brightness interpretation explicitly; do not silently change rendering to implement a current limit.
5. Each future behavior change should identify affected WC-SYS IDs, update this matrix, and add or revise executable checks. Retain blocked/unverified statuses until evidence actually exists.
6. See [Review.md](Review.md) for review fixes and remaining risks.
