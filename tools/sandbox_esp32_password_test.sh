#!/usr/bin/env bash
# Build and execute a dedicated ESP32 password-verifier probe. Test-only, not flashing.
set -euo pipefail
if [[ ${1:-} == --help || $# -lt 1 || $# -gt 2 || ( $# == 2 && $2 != --keep ) ]]; then
  echo "Usage: $0 RETAINED_BUILD [--keep]"
  echo 'First run ./tools/sandbox_esp32_build.sh --emulate --keep.'
  [[ ${1:-} == --help ]] && exit 0 || exit 2
fi
for cmd in python3 sha256sum sha512sum tar unshare mount setpriv timeout node readelf; do
  command -v "$cmd" >/dev/null || { echo "Missing required command: $cmd" >&2; exit 1; }
done
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { echo 'Requires x86-64 Linux' >&2; exit 1; }
node=$(command -v node)
[[ $node == /usr/* ]] || { echo 'Node must be under /usr for isolation' >&2; exit 1; }
bundle=$(cd "$1" && pwd -P)
for name in core/platforms/espressif32 core/packages/toolchain-xtensa-esp32 core/packages/framework-arduinoespressif32 python emu/esp32-emu-1.2.0.tgz; do
  [[ -e "$bundle/$name" ]] || { echo "Missing retained dependency: $name" >&2; exit 1; }
done
# A retained bundle is mutable: identify its key executables and package versions
# again. This does not authenticate the remaining files or official provenance.
(cd "$bundle/core/packages/toolchain-xtensa-esp32" && sha256sum -c <<'HASHES'
52dca444cca7be0e980060969d820f0223b3baa880dd798287111aabc08b4caa  bin/xtensa-esp32-elf-gcc
3f5a4f3495f7c1ad8aee81e8157d88ffed37044fc61c8097589462be6777ba14  bin/xtensa-esp32-elf-as
84bf2c7d64a5574f308b873799492fc8c7e39c05d41e5ca295b5799d122477f4  bin/xtensa-esp32-elf-ld
f2c595c463fc0de4fdf77863f86298d5aa530442b8f887c129f912222c0da3e4  libexec/gcc/xtensa-esp32-elf/8.4.0/cc1
c733360a497f2b3b500f1b203197f2428e43ac84bea451f9f675a43cbf3efe68  libexec/gcc/xtensa-esp32-elf/8.4.0/cc1plus
HASHES
)
python3 - "$bundle" <<'PYCODE'
import json, os, sys
root=sys.argv[1]
for path, expected in [('core/platforms/espressif32/platform.json','6.9.0'),
                       ('core/packages/framework-arduinoespressif32/package.json','3.20017.0'),
                       ('core/packages/toolchain-xtensa-esp32/package.json','8.4.0+2021r2-patch5')]:
    with open(os.path.join(root,path)) as f: actual=json.load(f)['version']
    if actual != expected: raise SystemExit(f'Unexpected version in {path}: {actual}')
PYCODE
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$HOME/.cache"
work=$(mktemp -d "$HOME/.cache/wallclock-esp32-password-XXXXXXXX")
keep=${2:-}
cleanup() {
  if [[ $keep == --keep ]]; then echo "Probe scratch retained: $work"; else rm -rf -- "$work"; fi
}
trap cleanup EXIT
mkdir -p "$work"/{root,core/packages,core/platforms,project/src,project/include,emu}
cp "$repo/test/security/password_target_probe.cpp" "$work/project/src/main.cpp"
cp "$repo/security/password_verifier.cpp" "$work/project/src/password_verifier.cpp"
cp "$repo/security/basic_auth.cpp" "$work/project/src/basic_auth.cpp"
cp "$repo/security/password_verifier.h" "$work/project/include/password_verifier.h"
cp "$repo/security/basic_auth.h" "$work/project/include/basic_auth.h"
cat > "$work/project/platformio.ini" <<'INI'
[env:esp32]
platform = espressif32@6.9.0
board = esp32dev
framework = arduino
build_flags = -std=gnu++17
build_unflags = -std=gnu++11
INI
echo 'Building ESP32 password-verifier probe using retained test-only dependencies...'
if ! unshare -Urnm --fork bash -euc '
  work=$1; bundle=$2; root=$work/root
  mount --make-rprivate /; mount -t tmpfs -o size=128m tmpfs "$root"
  mkdir -p "$root"/{usr,dev,core,project,python,tmp,home}
  mount --rbind /usr "$root/usr"; mount -o remount,bind,ro "$root/usr"
  ln -s usr/bin "$root/bin"; ln -s usr/lib "$root/lib"; ln -s usr/lib64 "$root/lib64"
  touch "$root/dev/null" "$root/dev/urandom"; mount --bind /dev/null "$root/dev/null"; mount --bind /dev/urandom "$root/dev/urandom"
  mount --bind "$work/core" "$root/core"
  mount --bind "$bundle/core/packages" "$root/core/packages"; mount -o remount,bind,ro "$root/core/packages"
  mount --bind "$bundle/core/platforms" "$root/core/platforms"; mount -o remount,bind,ro "$root/core/platforms"
  mount --bind "$work/project" "$root/project"
  mount --bind "$bundle/python" "$root/python"; mount -o remount,bind,ro "$root/python"
  /usr/sbin/chroot "$root" /usr/bin/setpriv --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs \
    /usr/bin/env -i HOME=/home PATH=/usr/bin:/bin LANG=C.UTF-8 PLATFORMIO_CORE_DIR=/core \
    PLATFORMIO_SETTING_ENABLE_TELEMETRY=no PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=/python \
    python3 -m platformio run -d /project -e esp32 -j 2
' bash "$work" "$bundle" > "$work/build.log" 2>&1; then
  echo 'Target probe build failed:' >&2; tail -80 "$work/build.log" >&2; exit 1
fi
tail -12 "$work/build.log"
readelf -h "$work/project/.pio/build/esp32/firmware.elf" | grep -q 'Machine:.*Tensilica Xtensa' || { echo 'Not an Xtensa ELF' >&2; exit 1; }
cp "$bundle/emu/esp32-emu-1.2.0.tgz" "$work/emu/"
(cd "$work/emu" && echo 'c6a0ae5681c1dd9b3ff73d24254cfb4f79c9317ed9b8cfaf2a7ea3c21d46ea14071571d90863eba378d07315b9c73e401ae0d91b4de2cf9299b9b49972bfe3ef  esp32-emu-1.2.0.tgz' | sha512sum -c)
tar --no-same-owner --no-same-permissions -xzf "$work/emu/esp32-emu-1.2.0.tgz" -C "$work/emu"
cp "$repo/test/security/password_target_emulator.mjs" "$work/emu/run.mjs"
if timeout 60 unshare -Urnm --fork bash -euc '
  work=$1; bundle=$2; node=$3; root=$work/root
  mount --make-rprivate /; mount -t tmpfs -o size=128m tmpfs "$root"
  mkdir -p "$root"/{usr,dev,emu,build,tmp,home}
  mount --rbind /usr "$root/usr"; mount -o remount,bind,ro "$root/usr"
  ln -s usr/bin "$root/bin"; ln -s usr/lib "$root/lib"; ln -s usr/lib64 "$root/lib64"
  touch "$root/dev/null" "$root/dev/urandom" "$root/boot_app0.bin"
  mount --bind /dev/null "$root/dev/null"; mount --bind /dev/urandom "$root/dev/urandom"
  mount --bind "$work/emu" "$root/emu"; mount -o remount,bind,ro "$root/emu"
  mount --bind "$work/project/.pio/build/esp32" "$root/build"; mount -o remount,bind,ro "$root/build"
  mount --bind "$bundle/core/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" "$root/boot_app0.bin"
  mount -o remount,bind,ro "$root/boot_app0.bin"
  /usr/sbin/chroot "$root" /usr/bin/setpriv --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs \
    /usr/bin/env -i HOME=/home PATH=/usr/bin:/bin "$node" /emu/run.mjs
' bash "$work" "$bundle" "$node" > "$work/emulator.log" 2>&1; then
  grep -E 'PASSWORD PROBE PASSED|PASSWORD_TARGET_PASS' "$work/emulator.log"
else
  echo 'Target password probe failed; final diagnostics:' >&2; tail -80 "$work/emulator.log" >&2; exit 1
fi
