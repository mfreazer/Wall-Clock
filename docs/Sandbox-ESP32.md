# ESP32 sandbox build and emulator smoke test

The normal PlatformIO registry downloads were inaccessible in the review sandbox. For this Linux x86-64 sandbox, the following workaround builds the current `platformio.ini`, `src`, `include`, and (if present) `lib` files, including local edits, then optionally executes the image in an emulator:

```sh
./tools/sandbox_esp32_build.sh --emulate
```

Add `--keep` to retain the scratch directory and emulator log under `~/.cache`; otherwise the script removes it on exit. The command downloads sources from GitHub and Python packages from PyPI; `--emulate` also downloads from npm. It needs Git, Python/pip, tar, `unshare`, mount, `setpriv`, chroot, readelf, and several GB of free space; emulation additionally needs Node 18+, npm and timeout. Unprivileged user, mount and network namespaces must be enabled. This is a sandbox-specific fallback, not a replacement for a normal verified PlatformIO installation. If build inputs or dependencies in `platformio.ini` change, the explicitly staged sources and package versions in the script must be updated.

## Provenance and safety limitation

**Do not use the resulting binary for release or flashing.** The Xtensa GCC 8.4.0+2021r2-patch5 compiler is taken from the third-party `ivvil/rhodium` Git repository, commit `6cd22a00191d1bc7aa5ba9418c3655b9b0765577`, tree `d6de6d2e467b078b7680a1e7755e68ef145aa185`. The script pins the commit and checks SHA-256 of five key executables. These checks identify the mirror contents; they do **not** establish that they match the official compiler. The official PlatformIO archive SHA-256 listed in [the PlatformIO registry mirror manifest](https://github.com/ivankravets/pioreg-ci-mirror/blob/main/tools/toolchain-xtensa-esp32-linux_x86_64/manifest.json) is `58c9d6f867f93b6e55491c200cb539274c8ae3dccf76daf29428aa64dd54d727`, but the sandbox could not download that archive to compare it. A verified official toolchain and independent target rebuild remain required before release.

The script obtains `platform-espressif32` 6.9.0, Arduino ESP32 2.0.17, NeoPixel 1.12.3 and ArduinoJson 7.2.1 from their upstream Git repositories at pinned commits. It constructs local PlatformIO metadata, including the framework's PlatformIO version `3.20017.0` in place of upstream's `2.0.17`; this is not the original PlatformIO framework archive and has not been byte-compared to it. PlatformIO 6.1.18, SCons 4.8.1 and esptool 4.5.1 come from PyPI. Their transitive Python dependencies are not hash-locked, so this is not a fully reproducible or provenance-verified supply chain. The emulator npm tarball is pinned to 1.2.0 and a SHA-512 digest; that identifies the package, not its trustworthiness.

The build and emulation execute in separate user/mount/network namespaces, in chroots with a clean environment, no network, no host home directory, no capabilities and a copied working tree. Compiler/framework packages and emulator inputs are mounted read-only. This limits exposure to the third-party executable but is defense in depth, not proof of safety or authenticity. The initial package downloads and preparation happen outside that execution isolation.

## Observed result

On source revision `814e0f4`, the sandbox rebuilt the clock with its legacy HTTP listener disabled: PlatformIO reported 45,768 bytes RAM and 845,581 bytes flash, and the emulator emitted the disabled-web notice, provisioning prompt and invalid-credential rejection. The refreshed emulator-only firmware SHA-256 is `07944e4c590aba245f7feead5338b9651662960d1aef6ffa2c824260b10120ac`. This does not test network access or establish official compiler provenance; never flash this image. The independent byte-identical QA rebuild below was for an earlier image, not this refreshed snapshot.

Previously, on source revision `ef1c60f`, a fresh `--emulate --keep` run built and linked the `esp32dev` target with the configured library versions. `readelf` identified the output as ELF32 Tensilica Xtensa; PlatformIO reported 45,768 bytes of RAM and 845,545 bytes of flash (14.0% and 64.5% of its reported limits). The build produced an ESP32 image. Its embedded app descriptor reports `arduino-lib-builder`, `Mar 5 2024`, and `v4.4.7-dirty`; it comes from the prebuilt Arduino ESP32 SDK, not the sketch build. The IDF version is consistent with the pinned framework, but is not independent proof of toolchain or firmware provenance. The third-party emulator `esp32-emu` booted a 4 MB flash image assembled from the bootloader, partition table, `boot_app0.bin`, and firmware, and the firmware printed the provisioning prompt and rejected an invalid short Wi-Fi password over emulated serial. The [independent QA review](QA-Review-PR-3.md) rebuilt the same source and reported a byte-identical firmware image (`5969d951…`); that establishes reproducibility with the pinned mirror, not the mirror compiler’s official provenance.

This is a **limited emulator smoke test**, not proof that the firmware works on a physical ESP32. In particular, Wi-Fi/SNTP, HTTP/browser access, real NVS and power loss, complete time-zone/DST coverage and physical-target behavior, GPIO, RMT/LED timing, current, and electrical behavior have not been validated. Emulator diagnostics included unimplemented peripheral-reset warnings; they are captured in the retained log with `--keep`. The host suite and physical acceptance remain separate gates.

## Reusing the GitHub-hosted test image in a future session

The branch contains one deliberately committed, **emulator-only** snapshot in `sandbox-artifacts/esp32/` (firmware, bootloader, partition table, `boot_app0.bin`, and a manifest; about 870 KiB total). This is an explicit exception to the normal rule against generated binaries in Git: the review sandbox cannot reach GitHub's release-asset upload/download hosts, and the user chose a small Git-stored snapshot. It contains no pre-provisioned Wi-Fi credentials. It was compiled with the unverified third-party compiler described above: **never flash or release it**. Its source commit and SHA-256 values are recorded in `manifest.json`; the ESP32 framework boot files originate from the pinned upstream Arduino ESP32 checkout described above. Checksums identify the saved bytes, but are not independent proof of compiler, firmware, or manifest authenticity. The manifest also records the smoke script used for the snapshot.

In a new session, check out the branch containing this snapshot and run:

```sh
./tools/sandbox_esp32_cached_smoke.sh
```

The runner verifies the four image hashes and compares the current `platformio.ini`, all files and directory presence under `src`, `include` and `lib`, the build script, and the emulator smoke script to the recorded inputs. It refuses a stale or corrupted snapshot, including when an additional source file appears. It downloads the small, SHA-512-pinned `esp32-emu` 1.2.0 tarball from npm, then runs the same boot/invalid-password smoke test without fetching a compiler, downloading the framework, or rebuilding firmware. The emulator runs in an isolated chroot without network access or the host home directory; npm download occurs before isolation. `--keep` retains the emulator log under `~/.cache` for the current sandbox. The cache directory is not a durable cross-session store. Test the guard itself with:

```sh
python3 test/sandbox_artifact_test.py -v
```

This saves time only when the firmware build inputs match. If firmware build inputs or the smoke script change, **refresh the snapshot or delete it in the same PR**; do not merge a stale image. The required host runner automatically runs its guard tests when the snapshot directory is present. If the snapshot is deleted, it skips that optional group. Never use the old image after such changes. This cache is not a substitute for the native tests, a verified official-toolchain build, or hardware acceptance.

### Refreshing the snapshot intentionally

Each snapshot update increases Git history by roughly the image size, so update it only when reuse justifies the cost; do not add the much larger compiler, SDK, build directories, logs, credentials, or private data to Git. GitHub Releases or another proper artifact store would be preferable if both upload and download are reachable in the intended environment; GitHub release assets are not reachable from this sandbox.

1. Commit the intended `platformio.ini`, `src`, `include`, `lib` (if present), `tools/sandbox_esp32_build.sh`, and `tools/sandbox_esp32_smoke.mjs` changes on the session branch. The artifact creation command requires these inputs to be clean.
2. Run `./tools/sandbox_esp32_build.sh --emulate --keep`. Save the printed scratch-directory path. It builds the current inputs and checks the emulator smoke test; it may use several GB temporarily.
3. Run `python3 tools/sandbox_esp32_artifact.py create /path/to/retained/scratch`. The command checks the staged build inputs against the current committed files, requires a passing emulator log, copies the four images, and writes the manifest. This check detects accidental mismatches; it does not independently authenticate the toolchain or a tampered scratch directory.
4. Run `./tools/sandbox_esp32_cached_smoke.sh` and `python3 test/sandbox_artifact_test.py -v`. Review the manifest, provenance and changed files. Commit the five files under `sandbox-artifacts/esp32/` and push the current session branch. Do not publish the image as a release or describe it as hardware-verified.
5. Once no longer needed, remove the retained scratch directory printed in step 2. It is outside the repository and is not persisted by the sandbox snapshot.

## Target newlib time-zone probe

A separate test sketch exercises `setenv`, `tzset` and `localtime_r` linked with the same retained ESP32 toolchain/newlib and Arduino framework. It checks 19 fixed UTC-to-local conversions, including Toronto, Newfoundland and Sydney DST boundaries, fixed half-hour/second offsets, an extreme offset, long abbreviations and explicit transition seconds. The expected Toronto and Sydney boundary times were cross-checked independently with IANA zone data. It runs the real Xtensa instructions in `esp32-emu`, not host libc or the production sketch, and is not a physical-board test. The compiler and target libc are from the same **unverified third-party mirror** described above; checksums do not establish official provenance.

To reproduce, first prepare a fresh retained sandbox bundle with `./tools/sandbox_esp32_build.sh --emulate --keep`, then use its printed path:

```sh
./tools/sandbox_esp32_timezone_test.sh /path/to/retained/scratch
```

Pass `--keep` as a second argument to retain the probe build and emulator logs. The command verifies the pinned emulator tarball, checks package versions and five compiler executable hashes, and builds and executes the probe in separate isolated chroots. The retained bundle is mutable and these checks do not authenticate all its files. The probe must report `TZ_TARGET_PASS 19`; an injected incorrect expected value was rejected. The selected cases are not exhaustive validation of every accepted POSIX rule, and emulator success is not hardware, Wi-Fi/SNTP, NVS, or accuracy verification.

## Emulated NVS completed-write persistence

With a matching cached snapshot, run:

```sh
./tools/sandbox_esp32_cached_smoke.sh --persistence
```

This boots the actual cached firmware with erased, in-memory emulated flash, sends a **synthetic** SSID and password over emulated serial, verifies the completion message and changed bytes in the NVS partition, then starts a fresh emulator against the same flash and checks the firmware does not report missing or invalid stored credentials. The reboot phase also positively checks that the synthetic pair remains in the emulated NVS partition and appears in the fresh worker’s emulated DRAM without being resent over serial. This is evidence of a flash read, not proof that a specific NVS API or Wi-Fi connection succeeded. Finally it erases the emulated NVS partition and confirms the missing-credentials signal reappears as a negative control. The probe validates the expected partition layout first. An injected erase before the persistence reboot makes the test fail. The synthetic credentials never enter the committed image or real hardware.

The test reads the emulator-provided DRAM view after each boot; it does not instrument individual NVS API calls. A test mutation masking that RAM observation was rejected. The cached smoke and persistence tests are not currently run by GitHub CI: their unprivileged namespace requirements have not been verified on that runner, and the sandbox remains the immediate gate.

This covers a **completed** credential write and simulated reboot with the emulator's flash implementation. It does not prove Wi-Fi connection, real ESP32 flash behavior, atomicity, interrupted writes or power-loss recovery; physical tests remain necessary. `--keep` retains verbose emulator diagnostics under `~/.cache` in the current sandbox.
