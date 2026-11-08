# Implementation plan and milestones

**Status:** Proposed execution plan for owner review. This plan does not approve the provisional choices in `docs/Implementation.md`, change the normative draft in `docs/SyRS.md`, or claim any requirement is accepted. No dates or effort estimates are assigned because ownership, hardware availability, and acceptance criteria have not been set.

## Current position

The clock engine, ESP32 Arduino firmware, USB serial provisioning/configuration, embedded browser UI, and substantial host-side tests exist. The network web listener is intentionally disabled: the HTTP handlers are unauthenticated, while the password-verification and Basic-auth helpers under `security/` are not integrated. `docs/Traceability.md` records requirements as partial, blocked, or unverified; host tests and emulator runs do not establish physical-device acceptance. Hardware and several product requirements remain undecided.

## Milestones

### M0 — Approve product decisions and acceptance baseline

**Purpose:** Turn provisional design assumptions and SyRS TBDs into explicit, reviewable decisions before treating them as acceptance requirements.

**Work:**
- Decide the intended use and access boundary: network scope, who may view/change settings, and physical/USB access assumptions.
- Select the exact ESP32 board, LED type/count, power supply, wiring/protection, level shifting, and control hardware.
- Approve ring index-zero/orientation, direction, arbitrary-count mapping and hour rounding, timezone needs, button/UI functions, and configuration defaults.
- Define Wi-Fi/NTP outage and restart behavior, synchronization indication, time accuracy and display latency targets.
- Resolve the interpretation of brightness limits and additive collisions (WC-SYS-070); do not treat brightness as an electrical current limiter.
- Update `docs/SyRS.md` with approved requirement values and `docs/Implementation.md` with the selected design; record any out-of-scope requirement as explicitly deferred.

**Exit criteria:** Every decision needed for the first hardware acceptance run has an owner-approved answer, or a documented deferral with its impact. Requirements have measurable verification criteria where applicable; no provisional profile value is silently promoted to an approved requirement.

### M1 — Establish trusted build and approve security design

**Purpose:** Make development evidence trustworthy and select a complete access/provisioning design before enabling network configuration.

**Work:**
- Choose and document a trusted, reproducible ESP32 toolchain/dependency source and record versions and artifact hashes. The committed sandbox snapshot and third-party compiler fallback are emulator-only and must not be used for flashing or release.
- Write the security design for browser identity and transport, authentication/authorization of reads and writes, password/certificate provisioning, USB setup and recovery, credential storage/rotation, and failure behavior.
- Threat-model relevant local-network risks, including DNS rebinding, unauthorized configuration, resource exhaustion, credential exposure, and physical access.
- Review whether the existing `security/` helpers fit the approved design; they are building blocks, not a complete or integrated security solution.

**Exit criteria:** A reviewed design and threat model cover first setup, normal use, lost credentials, recovery, and failure states. A verified target build can be produced from pinned, trusted inputs. The listener remains disabled until M2’s security integration and tests pass.

### M2 — Complete firmware behavior and secure user configuration

**Purpose:** Implement the approved behavior and close software-level gaps while preserving the working USB recovery path.

**Work:**
- Implement the approved web access design end-to-end; protect every reachable route consistently and keep fail-closed behavior on setup or authentication errors.
- Implement approved time synchronization/holdover/restart behavior, settings validation and persistence, controls, status reporting, and UI behavior.
- Resolve implementation/requirement mismatches, including timezone scope, rendering brightness semantics, update cadence, and error/recovery behavior.
- Extend host and target tests for authorization, negative access cases, provisioning/recovery, persistence failures, configuration races, Wi-Fi/time failures, and firmware/UI contracts. Keep `docs/Traceability.md` synchronized with actual evidence.
- Run the required host gate, Debug/Release CTest configurations, and a trusted ESP32 target build.

**Exit criteria:** Required host tests and trusted target build pass. Security tests demonstrate that unauthenticated and unauthorized clients cannot access protected operations; provisioning and recovery work as designed. No unapproved access path is enabled. Software-only requirements have evidence and traceability status, with hardware-dependent checks clearly marked pending.

### M3 — Build and verify the physical prototype

**Purpose:** Validate the design on the selected hardware; simulated tests cannot establish electrical or physical behavior.

**Work:**
- Assemble against approved component datasheets and wiring criteria; inspect supply sizing, grounding, level shifting, protection, and thermal/current limits before powering the installation.
- Flash only a build made with the trusted toolchain from the identified source revision; record the source commit, configuration, tool/dependency versions, and image hashes.
- Verify boot, USB provisioning/configuration, NVS persistence and recovery, Wi-Fi/SNTP, timezone transitions, display mapping/colors/collisions, button behavior, and approved browser access on the actual device.
- Measure display update latency, timekeeping accuracy, LED signal behavior, current, voltage, and temperatures against the approved criteria. Exercise network loss, restart, and power interruption cases.
- Record test setup, procedure, expected/observed results, and blocked/not-run cases; update `docs/Traceability.md` and issue records.

**Exit criteria:** All planned prototype tests have recorded results. Safety, timing, security, and functional acceptance criteria pass, or failures have owners and dispositions. No host stub, emulator result, or successful compile is represented as physical verification.

### M4 — Release readiness and handoff

**Purpose:** Produce a reproducible, supportable release rather than treating a development build as accepted product software.

**Work:**
- Close or explicitly accept each remaining requirement gap and review finding; publish the known limitations and recovery procedure.
- Re-run the complete host suite and trusted target build against the release source revision; archive logs and hashes without credentials or other secrets.
- Review configuration defaults, credential handling, installation/electrical instructions, upgrade/recovery steps, and the final access policy.
- Create a release checklist and evidence bundle linking each requirement to implementation and verification records.

**Exit criteria:** Owner approves the release baseline; required tests and hardware checks pass; all residual risks and deferred items are documented and accepted; release artifacts are traceable to source and verified build inputs.

## Ongoing change and tracking rules

- Track each milestone as an issue or project item with an owner, status, dependencies, and evidence links. Dates and estimates should be added only after the owner confirms availability and scope.
- A milestone is complete only when its exit criteria are evidenced; code merged or a host test passing alone is not completion.
- For behavior changes, identify affected WC-SYS requirements, update tests and `docs/Traceability.md`, and retain `Blocked`, `Partial`, or `Unverified` status until the corresponding evidence exists.
- Keep security, hardware-safety, and trusted-build gates independent: passing one does not imply the others pass.

## Immediate next actions

1. Owner reviews and accepts or edits M0’s decision list; choose an accountable owner for each decision.
2. Select the intended security/access boundary and nominate reviewers for M1’s threat model and architecture.
3. Identify the board and LED/power hardware procurement path, then define measurable timing, accuracy, and electrical criteria.
4. Convert M0 decisions and milestone work into tracked issues; preserve blocked statuses until evidence is available.
