# QA review: local USB configuration branch

Reviewer: independent QA. Date: 2026-11-04.
Subject: `arena/106609fc-wall-clock`, head `3316caa` ("Refresh emulator snapshot for USB configuration"), five commits on top of `main` `7383ebd`. No pull request was open for this branch when the review ran, so this is a review of the branch rather than of a PR discussion.

Method: I re-executed the branch's own gates in a clean worktree (`git worktree add --detach /tmp/usb origin/usb`) on x86-64 Linux, then wrote my own probes for the parts its suite does not reach. The host suite ran with the pinned ArduinoJson 7.2.1 sources; the ESP32 builds used the sandbox's retained, unverified third-party compiler bundle.

The scope is the firmware/serial path only. No physical ESP32, Wi-Fi, SNTP, LED strip or network is available here, so nothing below is physical-hardware evidence.

## What the branch claims

1. The legacy unauthenticated HTTP listener is registered but never started or polled; browser configuration is unavailable.
2. Settings can be read and replaced over USB serial with a new `tools/serial_config.py` client and a documented `CONFIG PING|GET|BEGIN|CHUNK|COMMIT|CANCEL` protocol.
3. Web and serial updates share one validation/save/apply path: length, offset, revision, printable-ASCII and 2048-byte rules, replacement LED buffer allocated before saving, save before apply, and no change on any failure.
4. A refreshed emulator snapshot, plus new host, emulator and target probes.
5. A `security/` password-verifier and Basic-auth helper exists but is **not** integrated.

## Gates re-run, and what I got

| Command | Result |
| --- | --- |
| `ARDUINOJSON_SRC=/tmp/aj test/host/run_host_tests.sh --require-firmware` | `passed 11, failed 0, skipped 0`; the 11th group is the new `serial configuration tool`; `firmware_host` reported 1030 checks, 0 failures; the snapshot guard passed |
| CMake Release, `-DWALLCLOCK_REQUIRE_FIRMWARE_TESTS=ON` | `ctest` 8/8, including the new `serial_config_tool` (`test/host/CMakeLists.txt`) |
| `tools/sandbox_password_verifier_test.sh` | `password verifier: 35 checks, 0 failures` |
| `tools/sandbox_esp32_build.sh --emulate --keep` | `SMOKE TEST PASSED`; rebuilt `firmware.bin` SHA-256 `36ba0a80…5ede` |
| `python3 tools/sandbox_esp32_artifact.py verify` | inputs match the manifest; warns emulator-only, provenance unverified |
| `tools/sandbox_esp32_cached_smoke.sh --serial-config` | `SERIAL CONFIG PHASE PASSED: write`, `… reboot`, `TEST PASSED` |
| `tools/sandbox_esp32_cached_smoke.sh --persistence` | write / reboot / erased-control all passed |
| `tools/sandbox_esp32_password_test.sh <bundle>` | `PASSWORD_TARGET_PASS 9` |
| `tools/sandbox_esp32_https_link_test.sh <bundle> [--with-clock]` | `HTTPS LINK PROBE PASSED` in both modes |
| `ARDUINOJSON_SRC=/tmp/aj/src tools/sandbox_security_gate.sh` | **5 checks, 5 failures, exit 1** — the documented, intentional result |

Two independent checks on the numbers rather than the scripts:

- The rebuilt image is **byte-identical to the refreshed snapshot** (`36ba0a80…`), so the snapshot is reproducible from source with the pinned mirror. `Sandbox-ESP32.md` says the earlier byte-identical QA rebuild applies to an older image; it now applies to this one as well.
- I recomputed both embedded PBKDF2 known-answer vectors with Python `hashlib.pbkdf2_hmac`, including the vector with an interior NUL in the password. Both match, so the verifier's expected values are not self-referential.

Image growth is main → branch `852,128` → `854,048` bytes (+1,920) for the emulator snapshot.

## Evidence the branch's own suite did not cover

Three gaps looked worth closing rather than reporting as risks. All three passed.

**1. The protocol itself, beyond the harness's assertions.** A 17-case host probe (`#include` the firmware, drive `Serial`):

- a committed time zone reaches libc, not just the stored struct (`tzname` changes `UTC` → `EST` after a USB commit of `EST5EDT,M3.2.0/2,M11.1.0/2`);
- a blank line aborts an active transfer, and the abort is final (commit afterwards gives `CONFIG ERR not started`, configuration unchanged);
- a `WIFI` line interleaved into a transfer aborts it and still executes, and the aborted transfer cannot commit;
- `CONFIG PING` mid-transfer is answered and the transfer still completes and applies;
- a chunk past the declared length and a non-ASCII chunk are both rejected and both end the transfer;
- an unsupported time zone is refused at commit (`CONFIG ERR invalid settings`) with the stored NVS value and live struct both unchanged;
- `server.started == false` and `server.handleCalls == 0` throughout.

**2. The client ↔ firmware seam, which no test in the branch crosses.** (The three probe sources are in [qa/](qa/), with build commands.) `tools/serial_config.py` is tested against a hand-written Python pseudo-terminal server, and the firmware is tested against a hand-written Node client. I bridged the real firmware's serial stub to a PTY (≈40 lines) and ran the shipped client against the shipped firmware:

```
get                 → firmware defaults, exit 0
set (count 60→72)   → "Configuration saved.", exit 0; subsequent get shows 72
set count=999       → CONFIG ERR invalid settings, exit 1; get still 72
set (new time zone) → applied and read back
get /dev/does-not-exist → reported, exit 1
5/5 end-to-end checks passed
```

**3. The listener claim on the target.** The emulator UART from the retained run prints `Web configuration disabled pending secure HTTPS setup.` before the provisioning prompt, `src/main.cpp` contains no `server.begin()`, `server.stop()` or `handleClient()` call, and `security/` is not included anywhere in the firmware. The disabled listener is real, not only a host-stub assertion.

## Findings

No correctness defect turned up in validation, chunking, offset/length limits, revision staleness, cancel, allocation failure, storage failure or corrupt-config repair: every state invariant I tried held. The items below are about assurance and cleanup, not about wrong behaviour.

**F1 — Medium, assurance: the client/firmware contract is untested.** The suite's fakes encode the protocol twice, independently of the two implementations, and the coverage is asymmetric. A *firmware* wording change is caught by the Node emulator test; a *client* expectation change is only checked against the Python fake, so a real client could stop interoperating while every gate stays green. My PTY bridge above shows the current pair works; the fix is to keep that bridge (or an equivalent contract test) in the suite, or at minimum document the reply strings as a contract in one place. The failure mode is silent and only appears on a user's machine with a serial cable.

**F2 — Low: an abandoned transfer is never timed out.** `serialConfigActive` stays true until a commit, a cancel, a blank line, an interleaved non-`CONFIG` command or a new `BEGIN`. I verified the consequence is benign: `BEGIN` restarts cleanly and nothing else consults that state, so no timeout is required for safety, and the harness's coverage here is accurate. Worth a code comment rather than a change.

**F3 — Low, documented: read-then-begin is still a lost-update window.** The revision is captured at `BEGIN`, so a button press or another client between `serial_config.py get` and `set` is overwritten. The README states this ("If settings changed after you saved the JSON file but before the transfer began, read and review them again"), which is the right call for a single-user USB tool; the tests should not be read as covering it.

**F4 — Nit: `RETAINED_BUILD` in the scripts' usage text and in the docs is a placeholder, not a literal argument.** `./tools/sandbox_esp32_https_link_test.sh RETAINED_BUILD` fails with `cd: RETAINED_BUILD: No such file or directory`; both scripts take the retained bundle path. The docs should show `./tools/sandbox_esp32_https_link_test.sh <retained-bundle-path>`.

**F5 — Nit: two snapshot refreshes in one branch.** `a87f9b4` and `3316caa` each replace an ~850 kB image, so this branch's history carries about 1.7 MB to deliver one. That is consistent with the branch's own guidance but cheaper to land squashed.

**F6 — Note, not a defect: `CONFIG GET` returns the whole settings document, with no credentials.** I confirmed the payload is the `Config` struct only; Wi-Fi credentials stay in their separate NVS value. The trust assumption "USB path assumes trusted physical access" is documented and is the correct one for this protocol.

## What is still not verified

Unchanged by this review, and correctly described in the branch's docs:

- physical NVS write atomicity, power loss during a write, and real serial timing;
- Wi-Fi association, SNTP acquisition, holdover and accuracy;
- physical network access, browser behaviour and DNS rebinding;
- GPIO/RMT waveform, LED timing, current and electrical limits;
- third-party compiler provenance: the pinned mirror is unidentified, the emulator is authenticated only by an npm digest, and the snapshot is emulator-only — **do not flash it**;
- target cost of a repeated 2 kB transfer (heap fragmentation), untested beyond the size cap.

## Verdicts, kept separate

**Local USB configuration: accept for merge as an incremental step.** Every claim in the branch reproduced, the two snapshot/manifest relationships I could check are consistent, and the added end-to-end test closes the one seam where a real defect could have hidden silently. Nothing here supports a release: the feature is host- and emulator-verified only, and the hardware items above are still blockers.

**Password-protected browser interface: not implemented, and not implied by this branch.** The listener is disabled, no TLS or authentication is compiled in, `security/` is dead code until integrated, and the target prints that the web configuration is disabled. The access gate fails 5/5 exactly as `docs/Release-Readiness.md` says it does, and that failure is the honest signal that WC-SYS-058 remains unimplemented. A green host suite must not be read as progress on that requirement. The intended trust model, credential lifecycle and recovery procedure are still the owner's decisions, not QA's.

## Recommendation

Merge, keeping the access gate failing and visible, and add the client ↔ firmware contract test from §"Evidence the branch's own suite did not cover" (2). Then proceed to a verified official-toolchain build and the hardware acceptance plan in `docs/Release-Readiness.md`; neither can be substituted by any result in this review.
