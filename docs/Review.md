# PR 1 engineering review

This review builds on PR #1's prototype, not an accepted system design. The SyRS is unchanged; the decisions and safety limitations in Implementation.md still apply. Findings below describe the original PR and changes made during review.

## Addressed

| Severity | Finding | Change / evidence |
|---|---|---|
| High | The documented CTest command omitted the requirement-linked tests; the smoke test used `assert`, which vanishes in Release. | Register both test executables and use always-active checks. CTest passes in Release and Debug with sanitizers. Release CTest rejected independently injected seconds-priority and phase-reset defects. Both configurations were run in the sandbox; GitHub CI is not relied on. |
| High | NeoPixel `updateLength` frees the old pixel buffer before attempting allocation. An allocation failure could leave a persisted configuration with no usable strip. | For a count change, allocate a replacement with no pin assigned before saving; reject failures, then blank and destroy the old driver before initializing the new one. Other updates reuse the driver. The host harness exercises simulated allocation failure; hardware behavior remains untested. |
| High | A plausible system epoch was presented as successful network synchronization, even without an SNTP event. | Require an SNTP callback since boot and a plausible epoch. The status does not claim a recent sync. ESP32 callback integration remains untested. |
| Medium | Overflowing the serial buffer reset it mid-line, allowing the remaining suffix to be interpreted as a provisioning command. | Discard the entire oversized line until its newline. A host test verifies oversized-line handling; actual serial integration remains untested. |
| Medium | Wi-Fi SSID and password were stored separately without checking writes, permitting mismatched credentials after a partial update. | Save a validated pair as one NVS value; check the return value and retain read-only fallback for legacy keys. Host tests exercise failed writes and reboot from the stubbed store; real NVS failure and power loss remain untested. |
| Medium | Hour position in firmware ignored fractional seconds despite the core supporting them. | Use the fractional part from the same `gettimeofday` sample as the displayed second. A core fractional-position check was added; firmware timing remains untested. |
| Medium | The time-zone value could include control characters, including embedded NUL, causing disagreement with the C time-zone API. | Reject ASCII control characters in core validation, with native checks. This is not full POSIX TZ grammar validation. |
| Medium | A page with stale settings could undo a physical-button change, and offset bounds were not reflected in the form. | Conditional ETag/If-Match writes reject stale updates; the page detects changes without discarding edits and offers an explicit reload. Node UI and stubbed firmware tests cover stale and racing updates; target/browser integration remains untested. |
| Medium | Each refresh allocated a frame; every settings save blanked the strip. | Stream pixels directly and reuse the driver when count is unchanged. Native tests compare streamed output with independently constructed frames. Hardware behavior remains untested. |
| Medium | Dependencies floated and no automated build/test workflow existed. | Pin direct libraries and add native Debug/Release and ESP32 PlatformIO CI jobs. The sandbox is the short-term test gate; GitHub CI is not relied on. |

## Remaining release blockers and risks

- The legacy HTTP handlers are unauthenticated and vulnerable to any LAN client and potentially DNS rebinding. The HTTP listener is temporarily disabled, so browser configuration is unavailable; host tests check that it is not started or polled, but physical network behavior remains unverified. Do not re-enable the handlers until protected access is implemented. JSON-only writes and the absence of CORS are not authentication.
- The ESP32 target compiles and boots to a serial smoke test in the sandbox, but the compiler is from an unverified third-party mirror and the framework was assembled from upstream sources rather than the official PlatformIO archive. See [Sandbox-ESP32.md](Sandbox-ESP32.md). A verified official-toolchain build and hardware tests remain release blockers. The host harness exercises JSON parsing, mocked HTTP, simulated NVS write failures/reboot, callback and GPIO paths; real HTTP/browser, NVS power-cycle, Wi-Fi/SNTP, GPIO and hardware tests have not been run. An emulated completed credential write survives a simulated reboot, with an erased-NVS negative control; this is not a real power-cycle or interrupted-write test.
- Time-zone validation intentionally supports only a documented POSIX subset. A dedicated ESP32/newlib emulator probe covers 19 representative cases, including Montréal, Newfoundland and Sydney DST boundaries; it is not exhaustive or physical-hardware evidence. Synchronization age, NTP failure reporting, holdover accuracy and a cold-start time source remain unspecified or unverified.
- Brightness is not a current limiter, and additive collisions can exceed the configured high level. Electrical sizing and the SyRS brightness interpretation need owner decisions and hardware verification.
- Geometry, control functions, outage/restart behavior, update latency, accuracy, hardware and power supply remain provisional pending owner approval. The selected WS2812B RGB profile does not implement RGBW.
- Request body size is checked after the web server has read the body. There is no robust resource-exhaustion protection, and LED allocation checks do not cover all heap allocations in JSON handling. There is no write-rate limit.

For requirement-specific gaps, see Traceability.md. Passing native tests is not system acceptance.

## QA follow-up

QA's report and host harness were cherry-picked from `fa92a7b` and `15133e7` onto this branch. The harness was adapted for the revised API and firmware (ETag/If-Match, SNTP callback, LED driver ownership and atomic credential value). It caught a C++17 `std::apply` name collision in the revised code, now fixed by renaming the function. Simulated tests were added for stale and missing preconditions, callback gating, allocation failure without a pin glitch, save failure, reboot, and oversized serial commands. The UI's actual POST is passed to the firmware harness by the runner.

The required host runner passed with ASan/UBSan: 10 groups with the cached snapshot (9 without), including 957 firmware checks and 50 UI checks. CTest passed all 7 tests in Release and Debug (host property and firmware suites sanitized). Mutating save-before-apply, stale revision enforcement, time-zone validation and the invalid-configuration display gate made the relevant host tests fail. These are host results, not hardware evidence.

## Time-zone follow-up

`Not/A/Zone` and other malformed or unsupported values are rejected. A restricted, documented POSIX grammar is checked before web saves and at boot. Previously persisted invalid or newly unsupported values put the device into an explicit recovery state rather than silently displaying UTC; the display stays dark until a valid configuration is saved. Host tests cover invalid inputs, simulated persisted-data recovery, and Montréal DST boundaries using host libc. An ESP32/newlib emulator probe now tests representative accepted rules and DST boundaries; broader rules and real NVS recovery remain unverified.

The DNS-rebinding, unauthenticated access, early-body-limit and write-endurance findings remain open.

## PR #2 QA follow-up

QA's re-review and addendum are preserved in [QA-Review-PR-2.md](QA-Review-PR-2.md) as historical evidence (commit `aed15bd`). The two previously overlapping workflows have been consolidated; the supplemental workflow requires the real ArduinoJson-backed firmware harness and uses strict CMake configuration, while the sandbox remains the immediate gate. The snapshot guard now checks the smoke script and is included in the host runner when the snapshot exists; a stale snapshot must be refreshed or removed in the same PR. The supplemental workflow runs on pushes to `main` and on pull requests, not on non-PR branch pushes; this avoids duplicate runs for same-repository PR branches. As supplemental evidence, [GitHub Actions run 41366194736](https://github.com/mfreazer/Wall-Clock/actions/runs/41366194736) reported success for both jobs on `997500c`; its log archive was inaccessible in this sandbox, so compiler provenance was not independently checked. The sandbox remains the immediate gate.

The independent [PR #3 QA review](QA-Review-PR-3.md) reproduced the emulator build and time-zone probe, including a byte-identical firmware image. Supplemental [GitHub Actions run 41632608872](https://github.com/mfreazer/Wall-Clock/actions/runs/41632608872) reported success for both jobs on `d956936`, including the PlatformIO target build. Its log archive was inaccessible here, so the individual compiler files or checksums were not independently verified. The CI build does not authenticate the separate third-party compiler used for emulation.

A later follow-up adds `ledBufferAvailable` to `/api/status` and a UI warning for a failed LED buffer allocation, with host fault-injection and recovery tests. This field reports only allocation state, not that a strip is connected, powered, or emitting light. The nonstandard test `assert` override and hardware/security limitations remain open.
