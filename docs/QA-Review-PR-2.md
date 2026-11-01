# QA re-review of PR #2

Reviewer: QA. Scope: independent verification of draft PR #2
(`arena/fed30e21-wall-clock`, head `6486a28`), which supersedes PR #1.

Status at review time: **draft**. Verdict: firmware changes verified as described;
the CI added with them does not run the test that proves them. Two blocking items,
both in CI; one observability gap in firmware; four minor items.

Posted as pull-request review `5729813629` with six inline comments.

## Claim-by-claim

| Claim in the PR body | Independent result |
| --- | --- |
| Host runner: 9 checks passed, 0 failed, 0 skipped | Confirmed: 932 stubbed firmware checks, 46 UI checks |
| CTest 7/7 in Release and Debug | Confirmed, but only with `-DWALLCLOCK_ARDUINOJSON_DIR` set |
| Malformed and unsupported time zones rejected | Confirmed: 2,551 checks, 0 failures |
| Invalid persisted settings leave the ring dark until a valid save | Confirmed: 54 checks, 0 failures |
| Injected defects are caught | Confirmed, but by `firmware_host` only for the fail-closed defect |

## Evidence

All runs on a clean worktree of `6486a28`, with ArduinoJson v7.2.1 supplied
locally. Independent tests were written for this review; they are described below
so the results can be reproduced or disputed.

### Time-zone validator

Corpus of 48 curated rules plus a 20,000-case mutation fuzz over the accepted
grammar. The invariant is stronger than "accept/reject": every rule the validator
accepts must demonstrably change libc behaviour (`tzname`, `tm_isdst`, offset via
`mktime`), so a rule that is accepted but applied as UTC counts as a failure.
2,472 accepted mutants were cross-checked this way. Result: **2,551 checks,
0 failures**.

Accepted and obeyed: `EST5EDT,M3.2.0/2,M11.1.0/2`, `CET-1CEST,M3.5.0,M10.5.0/3`,
`AEST-10AEDT,M10.1.0,M4.1.0/3`, `IST-5:30`, `NST3:30NDT,M3.2.0,M11.1.0`,
`EST5EDT,M3.2.0,M11.1.0` (implicit DST offset = standard + 1 h, per POSIX).
DST transitions land on the documented instants: 2026-03-08 07:00 UTC and
2026-11-01 06:00 UTC for Toronto-shaped rules; the southern-hemisphere rule
inverts correctly.

Rejected: IANA names (`America/Toronto`, `Not/A/Zone`), `<-03>3`, bare `EST5EDT`,
`J60`, month 0/13, week 0/6, weekday 0/8, `/24`, `/-1`, trailing space, tab,
newline, junk, embedded NUL, 97 characters. `/-1` and `/24` are the interesting
ones: `/24` is legal POSIX (0..167) but outside the documented grammar, so the
rejection is deliberate, not a defect.

### Fail-closed recovery and write preconditions

**54 checks, 0 failures.** Covered: fresh boot; an invalid stored zone entering
repair with a dark ring, `configurationNeedsRepair` in `/api/status`, an inert
button that does not overwrite the stored value, and defaults (not the bad value)
returned by `GET /api/config`; a valid save clearing repair and the cleared state
surviving a reboot; a corrupt JSON blob behaving identically; `428` without
`If-Match`, `412` on a stale revision, `200` returning a new ETag, and rejected
writes never advancing the revision; a physical button press invalidating an open
page; allocation failure returning `503` while the previous strip stays installed
and keeps refreshing; NVS unopenable returning `500` with repair retained and no
false "saved".

### Embedded web UI

**28 checks, 0 failures**, run against the script the firmware actually serves.
The point of interest is the failure mode flagged in the PR #1 review — a
background poll destroying edits in progress. It does not happen: an unchanged
ETag keeps the edit and leaves save enabled; a changed ETag disables save, shows
the reload prompt, and leaves the typed value in the field; no POST is issued
while stale; reload restores device values; a successful save sends `If-Match`
with the current revision, adopts the returned ETag and does not immediately
report a conflict; `412` disables save and explains why; `low >= high` is rejected
client-side; `offset.max` follows `count`.

### Fault injection

| Injected defect | Detected by |
| --- | --- |
| `valid()` reverted to the pre-fix rule | `requirements`, `firmware_host`; independent harness: 10 failures |
| `!configurationNeedsRepair` removed from the render guard | `requirements`, `firmware_host`; independent harness: 10 failures |
| Seconds priority removed from `renderEach` | `engine`, `requirements`, `engine_properties`, `firmware_host` |

`engine_test.cpp` now defines an always-on `assert`, so Release no longer runs it
vacuously. P1-1 from the PR #1 review is closed in substance, not worked around.

### Target API surface (substitute for the blocked build)

`src/main.cpp` compiles cleanly (`-std=gnu++17 -Wall -Wextra`, no warnings)
against the **real** Adafruit_NeoPixel 1.12.3 header: `setPin(int16_t)`,
`getPixels()`, `numPixels()`, the `int16_t pin` constructor and the destructor all
exist. Against arduino-esp32 2.0.17 (shipped by `espressif32@6.9.0`):
`WebServer::hasHeader(String)`, `sendHeader`, `collectHeaders`;
`Preferences::isKey/putString/begin`. Against the pinned ESP-IDF v4.4.7,
`esp_sntp.h` declares `sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t)`
with `typedef void (*sntp_sync_time_cb_t)(struct timeval *tv)`, matching the
firmware's callback exactly.

This is not a target build. It does remove the largest silent-failure risk — a
driver or core API that moved between versions.

## Blocking

1. **`.github/workflows/ci.yml` skips the firmware harness.** With no
   `-DWALLCLOCK_ARDUINOJSON_DIR`, CMake registers 6 tests instead of 7 and prints a
   single advisory line. Demonstrated: with the fail-closed render guard removed,
   that job reports `100% tests passed out of 6`. The same tree with an ArduinoJson
   path fails `requirements` and `firmware_host`. Either supply the path (as
   `native.yml` does) or make a missing `firmware_host` a hard failure.
2. **Two overlapping workflows.** `ci.yml` and `native.yml` both run on every push
   and PR with different coverage: the sanitizer Debug leg and the full host runner
   exist only in `native.yml`; the target build exists only in `ci.yml`. Consolidate.

## Non-blocking

3. `src/main.cpp:101` — a boot-time LED allocation failure is only reported on
   serial. `/api/status` then returns `synchronized: true` with no frames produced;
   the page says "Time acquired" while the ring stays dark. A `displayAvailable`
   or `repairReason` field would surface it. Behaviour is otherwise correct:
   saving settings reallocates and the ring resumes.
4. `.gitignore` — `.deps/` is not ignored although `native.yml` clones into it.
5. `test/web_ui_test.js:5` — reads `include/web_ui.h` relative to the working
   directory; use `__dirname`.
6. `test/engine_test.cpp:3` — redefining `assert` works but is fragile and reads
   misleadingly; a `CHECK()` macro or `-UNDEBUG` would be clearer.
7. The Wi-Fi credential validator rejects a 64-hex PSK (a legal ESP32 raw key) and
   WPA3-only key lengths. Acceptable; worth documenting.

## Branch hygiene

PR #2 contains PR #1 as a subset. Close PR #1 unmerged rather than merging both;
merging both collides in `CMakeLists.txt`, `README.md` and `docs/`.

## Not verified, and why

No target build in this environment: PlatformIO's package registry is blocked in
the sandbox, so the `pio run -e esp32` job added in `ci.yml` is the path to that
evidence. No hardware, GPIO or frame-timing measurements, no NVS persistence
across power loss, no electrical or thermal checks, no wall-clock accuracy or
drift measurements, no Wi-Fi reconnection behaviour, and no browser testing on
real devices.
