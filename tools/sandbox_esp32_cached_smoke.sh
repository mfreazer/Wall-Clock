#!/usr/bin/env bash
# Run the Git-stored, test-only ESP32 image; do not build or flash it.
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  echo "Usage: $0 [--keep]  (run cached-image emulator smoke test)"
  exit 0
fi
keep=0
if [[ ${1:-} == --keep && $# == 1 ]]; then keep=1; elif (( $# )); then echo "Unknown arguments" >&2; exit 2; fi
for cmd in node npm python3 sha512sum tar unshare mount setpriv timeout; do
  command -v "$cmd" >/dev/null || { echo "Missing required command: $cmd" >&2; exit 1; }
done
[[ $(uname -m) == x86_64 && $(uname -s) == Linux ]] || { echo 'Requires x86-64 Linux' >&2; exit 1; }
node=$(command -v node)
[[ $node == /usr/* ]] || { echo 'Emulation requires Node under /usr for isolation' >&2; exit 1; }
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$HOME/.cache"
work=$(mktemp -d "$HOME/.cache/wallclock-esp32-smoke-XXXXXXXX")
cleanup() { if (( keep )); then echo "Smoke log retained: $work/emulator.log"; else rm -rf -- "$work"; fi; }
trap cleanup EXIT
mkdir -p "$work"/{emu,images,root}
# Copy first, then verify exactly the images to be executed and the current build inputs.
cp "$repo/sandbox-artifacts/esp32/"{firmware.bin,bootloader.bin,partitions.bin,boot_app0.bin} "$work/images/"
python3 "$repo/tools/sandbox_esp32_artifact.py" verify --images-dir "$work/images"
printf '%s\n' 'Fetching SHA-512-pinned esp32-emu 1.2.0 from npm (no compiler download).'
(cd "$work/emu" && npm pack esp32-emu@1.2.0 --silent >/dev/null)
(cd "$work/emu" && echo 'c6a0ae5681c1dd9b3ff73d24254cfb4f79c9317ed9b8cfaf2a7ea3c21d46ea14071571d90863eba378d07315b9c73e401ae0d91b4de2cf9299b9b49972bfe3ef  esp32-emu-1.2.0.tgz' | sha512sum -c)
tar --no-same-owner --no-same-permissions -xzf "$work/emu/esp32-emu-1.2.0.tgz" -C "$work/emu"
cp "$repo/tools/sandbox_esp32_smoke.mjs" "$work/emu/run.mjs"
if timeout 60 unshare -Urnm --fork bash -euc '
  work=$1; node=$2; root=$work/root
  mount --make-rprivate /; mount -t tmpfs -o size=128m tmpfs "$root"
  mkdir -p "$root"/{usr,dev,emu,build,tmp,home}
  mount --rbind /usr "$root/usr"; mount -o remount,bind,ro "$root/usr"
  ln -s usr/bin "$root/bin"; ln -s usr/lib "$root/lib"; ln -s usr/lib64 "$root/lib64"
  touch "$root/dev/null" "$root/dev/urandom" "$root/boot_app0.bin"
  mount --bind /dev/null "$root/dev/null"; mount --bind /dev/urandom "$root/dev/urandom"
  mount --bind "$work/emu" "$root/emu"; mount -o remount,bind,ro "$root/emu"
  mount --bind "$work/images" "$root/build"; mount -o remount,bind,ro "$root/build"
  mount --bind "$work/images/boot_app0.bin" "$root/boot_app0.bin"; mount -o remount,bind,ro "$root/boot_app0.bin"
  /usr/sbin/chroot "$root" /usr/bin/setpriv --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs \
    /usr/bin/env -i HOME=/home PATH=/usr/bin:/bin "$node" /emu/run.mjs
' bash "$work" "$node" >"$work/emulator.log" 2>&1; then
  grep -E 'SMOKE TEST PASSED|Provision: WIFI|Invalid Wi-Fi credentials' "$work/emulator.log"
else
  echo 'Emulator smoke test failed; final diagnostics:' >&2
  tail -80 "$work/emulator.log" >&2
  exit 1
fi
