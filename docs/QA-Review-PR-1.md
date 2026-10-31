# QA review — PR #1 "Implement clock prototype with SyRS traceability and native tests"

| | |
|---|---|
| Reviewed head | `c9b3605` (`arena/715c6764-wall-clock`) |
| Base | `a3b161a` (`main`) |
| Scope | 11 files, +507/−3 |
| Reviewed by | QA |
| Date | 2026-10-31 |

**Verdict: no blocking defect found in the clock engine or the firmware logic — but one gate-repair fix is required before merge, because the advertised verification gate cannot fail.**

The implementation is unusually disciplined for a prototype: provisional decisions are isolated, the SyRS is untouched, and the traceability matrix states its own limits instead of claiming compliance. Everything the PR claims it ran, I was able to reproduce. The problem is what the PR does *not* claim but still implies: `cmake -S . -B build && ctest` looks like a test gate, and it is not one.

---

## 1. How this was reviewed

| Layer | Method | Environment |
|---|---|---|
| Native engine/tests | Built and ran both test binaries; independent property tests; mutation testing; CMake/CTest | g++ 12.2, CMake 4.4.4 |
| Firmware (`src/main.cpp`) | Compiled with `-std=gnu++17 -Wall -Wextra` against **real ArduinoJson 7.2.1** and host stubs for Arduino/NeoPixel/WiFi/WebServer/Preferences, then executed: HTTP routes, NVS, button, serial provisioning, render loop | ASan + UBSan |
| Web UI (`include/web_ui.h`) | Extracted the real HTML + JS, executed under a minimal DOM shim in Node 22 and captured the actual POST payload | Node 22 |
| Target firmware build | **Attempted and failed** — `espressif32@6.9.0` cannot be fetched in this sandbox (`HTTPClientError`) | PlatformIO 6.2 |
| Hardware | Not available | — |

Limits of this review: the firmware was compiled against stubs, not the xtensa toolchain, so target-only differences (newlib `TZ` semantics, ESP32 heap, RMT timing) are **not** covered. Rx/tx with a real ESP32 remains unverified — as the PR itself states.

## 2. What was executed, and what it showed

| Check | Result |
|---|---|
| `g++ -std=c++17 -Wall -Wextra -Wpedantic test/requirements_test.cpp` | PASS, 4/4 groups, zero warnings |
| Same with `-DNDEBUG` | PASS (checks are exception-based, as documented) |
| `g++ … test/engine_test.cpp` with and without `-DNDEBUG` | PASS, zero warnings |
| `cmake -S . -B build && ctest` | PASS — **1 test registered** (`engine` only) |
| Independent property tests (below) | 0 failures |
| Firmware compile (`-Wall -Wextra`, real ArduinoJson 7.2.1) | Clean, no warnings |
| Firmware HTTP integration harness | 3 605 assertions, 0 failures |
| Browser UI contract test | POST body has exactly the 13 keys the firmware requires |
| `git diff --check` | Clean |

Independent property tests written for this review, beyond what the PR already asserts:

* `mapPosition` bounds for **all** N = 1…300 × both directions × offsets {0, N/2, N−1} over all 43 200 whole-second hour positions — no index ever outside `[0, N)`.
* Clockwise mapping is monotone non-decreasing around the dial (no index jumps > 1).
* For every N ≥ 60 the 60 minute positions are distinct.
* Brightness never leaves `[low, high]`; `hour + minute == low + high` for 2 000 phases × 9 level/period combinations.
* One full period returns the phase to its start for P = 3, 4.5, 6 s.
* Changing the period changes neither phase nor instantaneous brightness (WC-SYS-038).
* `scale`/`add` saturate and never wrap; frame size == `count`; seconds win on every shared pixel for N ∈ {1, 2, 7, 60, 61, 144, 300}.

Firmware behaviour confirmed by running `setup()`/`loop()` against stubs:

* 4 routes registered; `GET /api/config` returns 13 members and is accepted verbatim by `POST /api/config` (the UI's reload-then-save path).
* 25 rejection cases behave correctly: wrong media type → 415, >2 048 bytes → 413, malformed/array/empty body → 400, 14 keys → 400, string-typed colour → 400, `low == high` → 400, `count: 60.5` → 400, `count: true` → 400, `offset == count` → 400, empty/newline timezone → 400. **No silent coercion** — the "never coerce" comment holds.
* NVS write failure → 500 and the active config is unchanged.
* Successful save → persisted → cold boot reloads it; corrupt NVS → defaults.
* 50 frames/s measured in the render loop with 60 LEDs; ring dark before a plausible epoch; no out-of-range `setPixelColor`.
* `TZ` change through `apply()` really does move local time (12:00 UTC → 08:00 EST), so the `setenv`/`tzset` plumbing is sound on newlib-like libc.
* Button toggles idle exactly once per press, persists, and does **not** toggle on release.
* Serial `WIFI <ssid>\t<pass>` provisioning stores credentials and starts association.
* ASan/UBSan clean.

Browser UI, executed for real:

* `refresh()` populates numbers, checkboxes and colours (`#ff6414` for `[255,100,20]`).
* Submit emits exactly `count, offset, clockwise, timezone, period, low, high, idle, am, pm, minute, second, background` with correct types — the wire contract matches `decode()`.
* UI output goes through `textContent`, so server error strings cannot inject markup.

## 3. Findings

### P1 — must fix before merge

**P1-1. The CMake/CTest gate cannot fail. `test/requirements_test.cpp` is never built.**

`CMakeLists.txt:5-8` registers only `engine_test`, and `test/engine_test.cpp` is built entirely from `assert`. CMake does not define a build type, so a plain `cmake -S . -B build` keeps asserts; but a `Release` build (`-DNDEBUG`) compiles them out, leaving a test that executes almost nothing and **always exits 0**.

Reproduced by injecting two realistic defects into a copy of `clock_engine.h` (seconds priority removed; oscillator phase reset on every tick):

```sh
# defaults: the defect IS caught
cmake -S . -B build && cmake --build build && ctest --test-dir build     # engine: Subprocess aborted → FAIL
# release: the same broken engine passes
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && ctest --test-dir build  # "100% tests passed out of 1"
```

With the same two defects, `requirements_test.cpp` fails three groups — it is the only executable evidence in this PR, and the default build path never runs it. This is the single highest-value fix in the PR, and it is small:

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

foreach(t engine_test requirements_test)
  add_executable(${t} test/${t}.cpp)
  target_include_directories(${t} PRIVATE include)
  target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
```

`-UNDEBUG` keeps the legacy assert-based test honest in Release builds, and `-Werror` protects the "zero warnings" property the PR relies on.

**P1-2. Nothing enforces any of this in CI.**

There is no `.github/` directory, so the PR's evidence is only reproducible on a developer's machine, by hand. A 15-line workflow running the two native tests on every push would make the matrix in `docs/Traceability.md` meaningful:

```yaml
name: native
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
      - run: ctest --test-dir build --output-on-failure
```

(With P1-1 applied, that Release build is the strongest cheap check available without hardware.)

### P2 — should fix or explicitly accept

**P2-1. `src/main.cpp:67` — the 2 048-byte limit is cosmetic.**

The check runs after `server.arg("plain")`, which means the **whole** request body has already been buffered by `WebServer` (`readBytesWithTimeout(client, _clientContentLength, …)`, see `libraries/WebServer/src/Parsing.cpp` in arduino-esp32; the pinned `espressif32@6.9.0` ships the same design). A LAN client can POST hundreds of KB and exhaust the ESP32 heap before the 413 is ever produced. The endpoint is documented as trusted-LAN only, so this is a denial-of-service surface rather than a breach, but the current check reads as if it were a limit. Either say so in `docs/Implementation.md`, or reject on `Content-Length` before the body is read (custom `Uri`/middleware in newer cores).

The same endpoint writes NVS on **every** accepted POST, and the button writes NVS on every toggle (`src/main.cpp:49`). There is no rate limit or debounce on writes, so flash endurance (~100 k cycles) is exposed to any client on the LAN. Worth one line in the security section.

**P2-2. No `Host`/`Origin` check — DNS rebinding can rewrite the clock's configuration.**

The "JSON-only, no CORS" argument blocks ordinary cross-origin form posts (verified: `text/plain` → 415, `application/json; charset=utf-8` → 415), but it does not block DNS rebinding: after a rebind the attacker's origin *is* the device origin, so the browser sends `Content-Type: application/json` without a preflight and the POST is accepted. Validating that the `Host` header is the device's own IP/hostname closes this cheaply.

**P2-3. Timezone validation accepts any string (WC-SYS-056 gap).**

Confirmed: `{"timezone":"Not/A/Zone"}` returns 200, is persisted, is echoed back to the UI as accepted, and silently renders UTC because `tzset()` ignores an unparsable `TZ`. The traceability matrix already admits the grammar gap — but the user-visible outcome is "the clock says 10:00 and looks right", which is worse than a rejection. Minimum fix: reject strings that do not match a POSIX `TZ` shape (`[A-Za-z]{3,}|<…>` followed by an offset, optional DST rule), in `wallclock::valid()` so both paths get it.

**P2-4. Web UI and physical controls drift apart.**

`poll()` only queries `/api/status`, never `/api/config`. Press the button to toggle idle while the page is open and the page keeps showing the old state; save the page afterwards and idle is silently switched back. Re-fetch `/api/config` in the poll (or after each save) and re-render the form.

Also in the UI: the `offset` field has `min="0"` but no `max`, and the number of valid offsets depends on `count`, which the form never recomputes. A user editing `count` from 60 to 24 and leaving `offset` at 31 gets a generic 400.

**P2-5. Every settings save visibly blanks the ring.**

`src/main.cpp:51` (`apply`) does `strip.clear(); strip.show();` before applying the new config. Each save therefore emits one black frame (plus, on a count change, a reallocation). Small, but it is user-visible and avoidable by rendering the next frame first, or by only clearing when the pixel count grows/shrinks.

**P2-6. `render()` allocates at frame rate.**

`include/clock_engine.h:45` returns a fresh `std::vector<Color>` 50 times per second. On a device that must run for months, that is continuous heap churn and fragmentation, and `apply()` calls `updateLength()` which reallocs the pixel buffer too. A caller-supplied buffer (or a `std::array<Color, 300>`) removes both. This is also what makes the "runs forever" claim impossible to test cheaply.

### P3 — polish

* `README.md:166` "Native tests" documents only the CMake path (which omits `requirements_test.cpp`) and a single `g++` command for `engine_test`. Add the second command; today the reader who follows the README never runs the good test.
* `src/main.cpp:92` prints the device IP every 30 s forever; print once on connect plus on change.
* `src/main.cpp:80` ignores `putString` failures for `ssid`/`password`, unlike `save()`.
* `src/main.cpp:46` — `if(d.size()!=13)return false;` makes the API intolerant of additive change: any future field breaks every old client and vice versa. Consider requiring a known superset instead.
* `include/web_ui.h` — the document is missing `<html>`/`<head>`/`<body>`; `<meta charset>` sits in the body. It renders, but it is invalid HTML and will confuse validators and some proxies. No cache-busting header either, so a browser may serve a stale UI after a firmware update.
* `platformio.ini` — `test/` is host-oriented (`<cassert>`, `<iostream>`, exceptions, `main()`); `pio test -e esp32` will try to build it for the target. Add `test_ignore`/`test_filter` or move the host tests under a separate directory.
* `.gitignore` — add `CMakeCache.txt`, `CMakeFiles/`, `.vscode/`, `.DS_Store`, `*.pyc`.
* `background` (JSON/`Config`) vs "idle color" (SyRS WC-SYS-026) — one name would be kinder to readers of both.
* README's new `##` headings are the only Markdown headings in a document that otherwise uses plain lines; cosmetic inconsistency introduced by the PR.
* `Time.fraction` exists and the engine honours it, but the firmware never populates it (`src/main.cpp:88`), so the hour indication is quantised to one second. `docs/Traceability.md` says this — good — but it is worth making the firmware pass the sub-second part rather than documenting the gap.

## 4. Requirements coverage

The matrix in `docs/Traceability.md` is the strongest part of this PR: all 55 WC-SYS IDs are present, statuses are separated into core-tested/partial/blocked/unverified, and no requirement is claimed as accepted. Spot-checked claims were accurate, including "R is always active", "CMake registers only L", and "CMake/CTest was not executed" (true in the author's sandbox; I executed it, and it registers one test).

Two comments:

1. With P1-1 applied, the note in `docs/Traceability.md:116` must be updated — "CMake currently registers only L" will no longer be true.
2. WC-SYS-070 (brightness limits) is flagged for interpretation, and correctly so: additive blending can exceed `B_H`, and no aggregate current limit exists. That needs an owner decision, not a code change — as the PR says. The same category applies to idle brightness, which the code silently ties to `low` (`include/clock_engine.h:46`); the SyRS specifies the idle *colour* but never its *brightness*, so this is an undeclared provisional choice that the matrix does not list. Please add it to `docs/Implementation.md` §Geometry and rendering.

## 5. Documentation accuracy

Every factual claim I could test was true: 50 Hz target (measured 50 frames/s at 60 LEDs), NVS failure → error (500), corrupt NVS → defaults on cold boot, dark ring before a plausible epoch, `60 mA/pixel` ⇒ 18 A at 300 pixels, serial provisioning format, defaults for period and idle, and the TZ example string producing the expected offset. `README.md:12` also fixes a stale `SyRS.md` path from `main` — a genuine improvement.

The one overstated claim is structural rather than factual: the PR body's "Requirement-linked native checks pass" is true, but a reader will infer from the README that the project's test command covers it. It does not.

## 6. Recommendation

Merge after P1-1 (a 6-line `CMakeLists.txt` change), ideally with P1-2 and P2-3/P2-4. Everything else can follow. The engine itself is in good shape: it survived bounds, monotonicity, phase-continuity, saturation and frame-size property tests that go well beyond the PR's own suite, and the firmware's HTTP/config contract behaved correctly under 3 605 host-side assertions including 25 rejection cases.

If useful, I can contribute the harness used for this review — host stubs + real ArduinoJson wiring, the firmware integration harness, the property tests, and the DOM-shim UI test — as a follow-up PR, so that "firmware logic verified without hardware" becomes reproducible instead of a one-off.
