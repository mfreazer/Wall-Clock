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

On the review revision, a fresh `--emulate --keep` run built and linked the `esp32dev` target with the configured library versions. `readelf` identified the output as ELF32 Tensilica Xtensa; PlatformIO reported 45,768 bytes of RAM and 845,285 bytes of flash (14.0% and 64.5% of its reported limits). The build produced an ESP32 image. The third-party emulator `esp32-emu` booted a 4 MB flash image assembled from the bootloader, partition table, `boot_app0.bin`, and firmware, and the firmware printed the provisioning prompt and rejected an invalid short Wi-Fi password over emulated serial.

This is a **limited emulator smoke test**, not proof that the firmware works on a physical ESP32. In particular, Wi-Fi/SNTP, HTTP/browser access, real NVS and power loss, target newlib time-zone/DST semantics, GPIO, RMT/LED timing, current, and electrical behavior have not been validated. Emulator diagnostics included unimplemented peripheral-reset warnings; they are captured in the retained log with `--keep`. The host suite and physical acceptance remain separate gates.
