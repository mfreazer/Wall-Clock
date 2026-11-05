# QA follow-up: PR #4 (`8928aed`)

Reviewer: independent QA. Date: 2026-11-05.
Subject: PR #4, head `8928aed` ("Add QA serial protocol and client integration gates"), a single commit on top of the `3316caa` branch already reviewed in [QA-Review-USB-Config.md](QA-Review-USB-Config.md).

Purpose: check that the follow-up actually closes finding F1 from that review (the untested client ↔ firmware seam), that the earlier evidence still holds, and that nothing was quietly changed in firmware, snapshot or my report.

## Scope of the change

`git diff 3316caa..8928aed` touches `docs/`, `test/host/`, and three `tools/sandbox_*` usage strings. It does not touch `src/`, `include/`, `security/`, `tools/serial_config.py` or `sandbox-artifacts/`, so the PR-body claim that this commit changes tests and docs only is accurate. The emulator firmware SHA-256 is still `36ba0a80…` and the manifest still names `de4101f`.

`docs/QA-Review-USB-Config.md` is present and **byte-identical** to the version on my branch (`diff` empty). No findings were edited, softened or removed.

## Gates re-run on `8928aed`

| Gate | Result |
| --- | --- |
| `ARDUINOJSON_SRC=/tmp/aj test/host/run_host_tests.sh --require-firmware` | `passed 13, failed 0, skipped 0`; new groups `independent serial protocol probe` and `serial client to firmware PTY`; `firmware_host` 1030 checks, UI contract 50 checks |
| CMake Release, `-DWALLCLOCK_REQUIRE_FIRMWARE_TESTS=ON` | `ctest` 10/10, including `serial_protocol_probe` and `serial_client_firmware` |
| CMake Debug, `-DWALLCLOCK_SANITIZERS=ON` | configure and build clean; `ctest` 10/10 |
| `ctest --repeat until-fail:10 -R serial_` (sanitized) | 30 consecutive runs green |
| `python3 test/sandbox_artifact_test.py` | 9 tests OK |
| `tools/sandbox_password_verifier_test.sh` | 35 checks, 0 failures |
| `ARDUINOJSON_SRC=/tmp/aj tools/sandbox_security_gate.sh` | 5 checks, **5 failures**, exit 1 (unchanged, intentional) |
| `python3 tools/sandbox_esp32_artifact.py verify` | inputs match the manifest; emulator-only warning intact |

I confirmed the sanitizers are really active in the Debug build rather than merely requested: the serial binaries link `libasan.so.8` and `libubsan.so.1` and contain 43 `__asan`/`__ubsan` symbols.

CI ran on the PR head and reported success for both jobs (`host tests` 34 s, `ESP32 target build` 48 s). As with earlier runs, the job log archive is not retrievable from this sandbox, so I cannot confirm from CI output that the serial groups ran there rather than skipping. Locally they ran, not skipped.

## Is the new seam test substantive?

Yes. `test/host/serial_e2e_test.py` starts `serial_pty_bridge` (real `src/main.cpp` linked with the host stubs) and invokes the shipped `tools/serial_config.py` as a **separate process** against the printed PTY, then asserts defaults, a chunked update applied and read back, an invalid document rejected with no state change, a time-zone change, and a missing-device error. The bridge is my probe hardened with `writeAll` handling `EINTR`/`EAGAIN` through `poll`, which is an improvement over the version in `docs/qa/`.

`test/host/serial_protocol_probe.cpp` carries the same 17 checks I reported, with only the header comment and include path adapted; no assertion was weakened or dropped.

## Mutation checks, both directions

- **Client side, as claimed in the PR.** Renaming the chunk command in `tools/serial_config.py` (`CONFIG CHUNK` → `CONFIG CHUNKED`) and matching the Python fake: `serial_config_test.py` ran 4/4 green, while `serial_e2e_test.py` failed with `Serial configuration failed: CONFIG ERR unknown command`. Reproduced exactly. This is the failure mode F1 described, now caught.
- **Firmware side, my addition.** Renaming the firmware's success reply (`CONFIG CHUNK OK` → `CONFIG CHUNK DONE`): the probe reported 6 failures and the PTY test failed, the client surfacing the change as a 10 s response timeout. The gate is therefore sensitive in both directions, not only to client drift.

## Follow-ups from the earlier review

- F2 (no inactivity timeout) is now stated in `docs/Implementation.md`, including that `CONFIG BEGIN` or `CONFIG CANCEL` clears an abandoned transfer without saving.
- F4 (placeholder `RETAINED_BUILD` argument) is fixed: both scripts print `<retained-build-directory>` and the docs pass `"$BUNDLE"`. Related, the `sandbox_security_gate.sh` ArduinoJson resolution that tripped me up now accepts the repository root as well as `.../src`, and `ARDUINOJSON_SRC=/tmp/aj` works.
- F3 (read-then-begin lost-update window) and F5 (two snapshot refreshes in one branch) are unchanged, and both remain accurately documented or immaterial.

## Remaining observations

**N1 — Low, triage only.** A firmware-side reply change surfaces in the PTY test as a client timeout, not as a protocol mismatch. The test does fail, which is what matters; a diagnostic naming the expected reply would shorten investigation.

**Confirmed good, not a defect.** The new group cannot silently skip in the required gate: `skip()` converts to a failure under `--require-firmware`, and with `WALLCLOCK_REQUIRE_FIRMWARE_TESTS=ON` CMake fails configuration when Python 3 or POSIX pseudo-terminals are unavailable. A green `--require-firmware` run therefore does mean the serial integration executed.

## Verdict

The follow-up does what it claims. F1 is closed by a test that demonstrably catches client drift, the earlier gate results still hold, my report is preserved unedited, and firmware and snapshot are untouched. Nothing in this commit changes the two verdicts from the original review: the USB settings path is acceptable as an incremental merge, and the password-protected browser interface remains unimplemented — the listener is still disabled, `security/` is still not compiled into the firmware, and the access gate still fails 5/5 by design.

Still unverified, unchanged by this commit: physical NVS power-loss behaviour, real Wi-Fi/SNTP, physical serial and network behaviour, GPIO/RMT timing, electrical limits, compiler provenance, and the access-policy decision itself.
