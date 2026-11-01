# PR 1 engineering review

This review builds on PR #1's prototype, not an accepted system design. The SyRS is unchanged; the decisions and safety limitations in Implementation.md still apply. Findings below describe the original PR and changes made during review.

## Addressed

| Severity | Finding | Change / evidence |
|---|---|---|
| High | The documented CTest command omitted the requirement-linked tests; the smoke test used `assert`, which vanishes in Release. | Register both test executables and use always-active checks. CTest passes in Release and Debug with sanitizers. Release CTest rejected independently injected seconds-priority and phase-reset defects. CI is configured for both configurations but has not been run here. |
| High | NeoPixel `updateLength` frees the old pixel buffer before attempting allocation. An allocation failure could leave a persisted configuration with no usable strip. | For a count change, allocate a replacement with no pin assigned before saving; reject failures, then blank and destroy the old driver before initializing the new one. Other updates reuse the driver. The host harness exercises simulated allocation failure; hardware behavior remains untested. |
| High | A plausible system epoch was presented as successful network synchronization, even without an SNTP event. | Require an SNTP callback since boot and a plausible epoch. The status does not claim a recent sync. ESP32 callback integration remains untested. |
| Medium | Overflowing the serial buffer reset it mid-line, allowing the remaining suffix to be interpreted as a provisioning command. | Discard the entire oversized line until its newline. A host test verifies oversized-line handling; actual serial integration remains untested. |
| Medium | Wi-Fi SSID and password were stored separately without checking writes, permitting mismatched credentials after a partial update. | Save a validated pair as one NVS value; check the return value and retain read-only fallback for legacy keys. Host tests exercise failed writes and reboot from the stubbed store; real NVS failure and power loss remain untested. |
| Medium | Hour position in firmware ignored fractional seconds despite the core supporting them. | Use the fractional part from the same `gettimeofday` sample as the displayed second. A core fractional-position check was added; firmware timing remains untested. |
| Medium | The time-zone value could include control characters, including embedded NUL, causing disagreement with the C time-zone API. | Reject ASCII control characters in core validation, with native checks. This is not full POSIX TZ grammar validation. |
| Medium | A page with stale settings could undo a physical-button change, and offset bounds were not reflected in the form. | Conditional ETag/If-Match writes reject stale updates; the page detects changes without discarding edits and offers an explicit reload. Node UI and stubbed firmware tests cover stale and racing updates; target/browser integration remains untested. |
| Medium | Each refresh allocated a frame; every settings save blanked the strip. | Stream pixels directly and reuse the driver when count is unchanged. Native tests compare streamed output with independently constructed frames. Hardware behavior remains untested. |
| Medium | Dependencies floated and no automated build/test workflow existed. | Pin direct libraries and add native Debug/Release and ESP32 PlatformIO CI jobs. CI results are not yet available. |

## Remaining release blockers and risks

- The interface is unauthenticated HTTP, vulnerable to any LAN client and potentially DNS rebinding. JSON-only writes and the absence of CORS are not authentication. Use an isolated trusted network only; agree on an access policy and implement its controls before deployment.
- ESP32 compilation was attempted locally but PlatformIO could not download the platform package in this sandbox. The changes to firmware are **not build-verified**. The host harness exercises JSON parsing, mocked HTTP, simulated NVS write failures/reboot, callback and GPIO paths; real HTTP/browser, NVS power-cycle, Wi-Fi/SNTP, GPIO and hardware tests have not been run. CI must pass and target tests must be added before considering release.
- POSIX TZ strings are not fully validated; malformed rules may be interpreted unexpectedly. Define accepted syntax and test timezone/DST behavior. Synchronization age, NTP failure reporting, holdover accuracy and a cold-start time source remain unspecified or unverified.
- Brightness is not a current limiter, and additive collisions can exceed the configured high level. Electrical sizing and the SyRS brightness interpretation need owner decisions and hardware verification.
- Geometry, control functions, outage/restart behavior, update latency, accuracy, hardware and power supply remain provisional pending owner approval. The selected WS2812B RGB profile does not implement RGBW.
- Request body size is checked after the web server has read the body. There is no robust resource-exhaustion protection, and LED allocation checks do not cover all heap allocations in JSON handling. There is no write-rate limit.

For requirement-specific gaps, see Traceability.md. Passing native tests is not system acceptance.

## QA follow-up

QA's report and host harness were cherry-picked from `fa92a7b` and `15133e7` onto this branch. The harness was adapted for the revised API and firmware (ETag/If-Match, SNTP callback, LED driver ownership and atomic credential value). It caught a C++17 `std::apply` name collision in the revised code, now fixed by renaming the function. Simulated tests were added for stale and missing preconditions, callback gating, allocation failure without a pin glitch, save failure, reboot, and oversized serial commands. The UI's actual POST is passed to the firmware harness by the runner.

`test/host/run_host_tests.sh --require-firmware` passed with ASan/UBSan: all 7 reported checks passed, including 901 firmware checks and 46 UI checks. CTest passed all 6 tests in Release and Debug (host suites sanitized). Mutating save-before-apply and stale revision enforcement made the firmware harness fail. CI has not yet been checked; these are host results, not hardware evidence.

QA's malformed POSIX timezone example remains unresolved: `Not/A/Zone` may be accepted and render as UTC. A robust supported grammar and migration/fail-safe behavior for previously stored invalid values need to be defined and verified against ESP32 newlib. Do not rely on the timezone field for correctness until then. Host validation alone would not secure the unauthenticated API against direct LAN access; the DNS-rebinding and early-body-limit findings also remain open.
