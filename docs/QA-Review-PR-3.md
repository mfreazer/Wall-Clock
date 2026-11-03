# QA review of PR #3 (target probe, NVS persistence, display status)

Reviewer: QA. Scope: independent verification of draft PR #3
(`arena/6ccfed33-wall-clock`, head `d956936`), which follows merged PR #2.
Posted as pull-request review `5751936863`.

Verdict: every claim reproduced, including a byte-identical rebuild of the
committed image and a passing ESP32/newlib probe built and run here. No blocking
findings; four non-blocking notes.

## Evidence

All runs were on a clean worktree of `d956936` with ArduinoJson v7.2.1 supplied
locally. The ESP32 work was reproduced end to end rather than accepted from the
PR description.

| Claim | Result |
| --- | --- |
| Required host runner: 10 groups, 0 failed, 0 skipped | Confirmed: 957 firmware checks (11 scenarios), 50 UI checks |
| Strict CTest Release and Debug (sanitized) | 7/7 and 7/7 |
| Target newlib time-zone probe, 19 cases | Confirmed: `TZ_TARGET_PASS 19` |
| Injected wrong expectation rejected | Confirmed: `TZ_CASE_FAIL`, `TZ_TARGET_FAIL 1/19`, exit 1 |
| NVS completed write, simulated reboot, erased control | Confirmed: all three phases pass |
| Injected erase before the reboot detected | Confirmed: `PERSISTENCE PHASE FAILED: reboot` |
| `ledBufferAvailable` exposes failure and recovery | Confirmed independently |
| Mutations caught (buffer reported available, UI warning hidden) | Confirmed: 4 harness failures; 3 UI failures |

### The snapshot is reproducible

`./tools/sandbox_esp32_build.sh --emulate --keep` rebuilt the firmware from
source in this sandbox and produced a `firmware.bin` byte-identical to the
committed image:

```
5969d95197fb26012f0348246917804dc220788c44b5d1bdb3f6dc0694b8a238
```

That settles the question the guard could only partially answer: the stored image
really is this source built with the pinned toolchain and framework. PlatformIO
reported 45,768 bytes of RAM and 845,545 bytes of flash.

### The target probe is sound

Before trusting the probe, the oracle was checked independently: all 19 expected
UTC-to-local results match glibc under the same rules (`TZ`, `tzset`,
`localtime`), including the Toronto, Newfoundland and Sydney transition instants,
the `isdst` flags, half-hour and `-0:45:30` offsets, a 10-letter abbreviation,
`AAA+24`, and the two cases with explicit transition seconds. The real-zone cases
also agree with the IANA database. A deliberately flipped DST flag then produced
`TZ_CASE_FAIL` with the actual newlib result `2026-03-08 03:00:00 dst=1`,
matching glibc, so the probe both runs and discriminates.

### Display status

Independent probe against the firmware:

```
after failed boot alloc : {"connected":false,"configurationNeedsRepair":false,"ledBufferAvailable":false,"synchronized":false}
after sync, still dark  : {... "ledBufferAvailable":false,"synchronized":true}
frames while dark       : 0
save -> 200
after recovery          : {... "ledBufferAvailable":true,"synchronized":true}
renders after recovery  : yes
```

The gap reported in the two previous reviews is closed: the failure is visible
through the API and the page, `synchronized` no longer implies a working display,
and a settings save recovers it. Reporting the field as always `true` fails the
firmware harness; removing the UI warning fails the UI contract.

### Persistence

`./tools/sandbox_esp32_cached_smoke.sh --persistence` passed: completed write,
reboot against the same in-memory flash, erased-flash control. The write phase
compares NVS partition bytes before and after and looks for the synthetic pair,
which is what makes it a real persistence check. The synthetic credentials do not
appear in the committed image.

### CI

Run `41632608872` on `d956936` succeeded for both jobs (`host tests`,
`ESP32 target build`), so the official PlatformIO toolchain now has a green
compile of this firmware. Log archives are unreachable from this sandbox
(`results-receiver.actions.githubusercontent.com` is blocked), so only job
conclusions were verified, not step output.

## Notes raised (non-blocking)

1. `docs/Sandbox-ESP32.md:21` quotes the previous revision's flash figure
   (845,285); the refreshed build reports 845,545.
2. `docs/Review.md:45` cites only the `997500c` run; the current head also has a
   green run, and it includes the official-toolchain compile.
3. The cached smoke and `--persistence` are not in CI although both need only
   Node, npm and the committed snapshot; two steps in the host job would guard
   them on every PR. Depends on unprivileged user namespaces on the runner.
4. `test/target/persistence_emulator.mjs:52`: the reboot phase proves success by
   absence of error strings; a positive assertion would strengthen it.

Housekeeping: this QA branch still carries the pre-consolidation workflow file,
which is why pushes to it still appear as `push` runs in the Actions list.

## Not verified, and why

No physical hardware: real flash behaviour, interrupted writes and power-loss
atomicity, Wi-Fi/SNTP, target HTTP and browser behaviour, GPIO/RMT timing,
electrical safety, accuracy, holdover and drift remain untested. The
emulator-tested image still comes from the unverified third-party compiler
mirror. The unauthenticated HTTP interface still requires an access-policy
decision.
