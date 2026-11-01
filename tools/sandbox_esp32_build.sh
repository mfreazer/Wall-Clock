#!/usr/bin/env bash
# Build the current working tree without the PlatformIO registry. Linux sandbox only.
# The compiler is from a pinned third-party mirror, NOT an authenticated release.
set -euo pipefail

if [[ ${1:-} == --help ]]; then
  echo "Usage: $0 [--emulate] [--keep]  (optionally smoke-test boot; keep scratch files)"
  exit 0
fi
keep=0
emulate=0
for arg in "$@"; do
  case "$arg" in
    --keep) keep=1 ;;
    --emulate) emulate=1 ;;
    *) echo "Unknown argument: $arg" >&2; exit 2 ;;
  esac
done
if (( emulate )); then
  for cmd in node npm timeout sha512sum; do
    command -v "$cmd" >/dev/null || { echo "Missing required command: $cmd" >&2; exit 1; }
  done
  node=$(command -v node)
  [[ $node == /usr/* ]] || { echo 'Emulation requires Node under /usr for isolation' >&2; exit 1; }
fi
for cmd in git python3 tar unshare mount sha256sum readelf setpriv; do
  command -v "$cmd" >/dev/null || { echo "Missing required command: $cmd" >&2; exit 1; }
done
[[ $(uname -m) == x86_64 && $(uname -s) == Linux ]] || { echo 'Requires x86-64 Linux' >&2; exit 1; }
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$HOME/.cache"
work=$(mktemp -d "$HOME/.cache/wallclock-esp32-XXXXXXXX")
cleanup() {
  if (( keep )); then echo "Scratch directory retained: $work"; else rm -rf -- "$work"; fi
}
trap cleanup EXIT
printf 'Building working tree at %s (HEAD %s)\n' "$repo" "$(git -C "$repo" rev-parse HEAD)"
printf '%s\n' 'WARNING: unverified third-party compiler; output is for testing, not release or flashing.'
mkdir -p "$work"/{checkouts,core/platforms,core/packages,project,python,root}

# Fetch an exact commit, not an evolving tag. A sparse checkout avoids unrelated blobs.
fetch() {
  local name=$1 url=$2 commit=$3; shift 3
  git init -q "$work/checkouts/$name"
  git -C "$work/checkouts/$name" remote add origin "$url"
  git -C "$work/checkouts/$name" -c advice.detachedHead=false fetch -q --depth=1 --filter=blob:none origin "$commit"
  if (( $# )); then
    git -C "$work/checkouts/$name" sparse-checkout init --cone
    git -C "$work/checkouts/$name" sparse-checkout set "$@"
  fi
  git -C "$work/checkouts/$name" -c advice.detachedHead=false checkout -q --detach FETCH_HEAD
  [[ $(git -C "$work/checkouts/$name" rev-parse HEAD) == "$commit" ]] || { echo "Unexpected $name commit" >&2; exit 1; }
}
fetch platform https://github.com/platformio/platform-espressif32.git 268f561df4793d631d02e4ff5e67b6b11d1be4ec
fetch arduino https://github.com/espressif/arduino-esp32.git 5e19e086c43d0fa5e5a596497ff8f11a0a43f6c2 cores libraries tools/sdk/esp32 tools/partitions variants
fetch neopixel https://github.com/adafruit/Adafruit_NeoPixel.git aa798ff5e9bb9d7299190627d2454ccd6f599ae8
fetch arduinojson https://github.com/bblanchon/ArduinoJson.git f9fe8557f13d8949eafd49ebd93a2929c4e5065f
fetch mirror https://github.com/ivvil/rhodium.git 6cd22a00191d1bc7aa5ba9418c3655b9b0765577 .platformio/packages/toolchain-xtensa-esp32
[[ $(git -C "$work/checkouts/mirror" rev-parse HEAD:.platformio/packages/toolchain-xtensa-esp32) == d6de6d2e467b078b7680a1e7755e68ef145aa185 ]] || { echo 'Unexpected compiler tree' >&2; exit 1; }
compiler="$work/checkouts/mirror/.platformio/packages/toolchain-xtensa-esp32"
(cd "$compiler" && sha256sum -c <<'HASHES'
52dca444cca7be0e980060969d820f0223b3baa880dd798287111aabc08b4caa  bin/xtensa-esp32-elf-gcc
3f5a4f3495f7c1ad8aee81e8157d88ffed37044fc61c8097589462be6777ba14  bin/xtensa-esp32-elf-as
84bf2c7d64a5574f308b873799492fc8c7e39c05d41e5ca295b5799d122477f4  bin/xtensa-esp32-elf-ld
f2c595c463fc0de4fdf77863f86298d5aa530442b8f887c129f912222c0da3e4  libexec/gcc/xtensa-esp32-elf/8.4.0/cc1
c733360a497f2b3b500f1b203197f2428e43ac84bea451f9f675a43cbf3efe68  libexec/gcc/xtensa-esp32-elf/8.4.0/cc1plus
HASHES
)
# Keep PlatformIO, SCons and esptool separate from the third-party mirror.
python3 -m pip install --disable-pip-version-check --quiet --target "$work/python" 'platformio==6.1.18' 'SCons==4.8.1' 'esptool==4.5.1'
copy_checkout() { mkdir -p "$2"; tar -C "$1" --exclude=./.git -cf - . | tar -C "$2" -xf -; }
copy_checkout "$work/checkouts/platform" "$work/core/platforms/espressif32"
copy_checkout "$work/checkouts/arduino" "$work/core/packages/framework-arduinoespressif32"
cp -a "$compiler" "$work/core/packages/toolchain-xtensa-esp32"
mkdir -p "$work/core/packages/tool-scons" "$work/core/packages/tool-esptoolpy"
cp -a "$work/python/SCons" "$work/core/packages/tool-scons/"
cp "$work/python/bin/scons" "$work/core/packages/tool-scons/scons.py"
cp -a "$work/python/esptool" "$work/core/packages/tool-esptoolpy/"
cp "$work/python/bin/esptool.py" "$work/core/packages/tool-esptoolpy/esptool.py"
# Copy only build inputs (including edits); do not expose the repository or unrelated files.
inputs=(platformio.ini src include tools/sandbox_esp32_smoke.mjs)
[[ ! -d "$repo/lib" ]] || inputs+=(lib)
tar -C "$repo" -cf - "${inputs[@]}" | tar -C "$work/project" -xf -
libdeps="$work/project/.pio/libdeps/esp32"
copy_checkout "$work/checkouts/neopixel" "$libdeps/Adafruit NeoPixel"
copy_checkout "$work/checkouts/arduinojson" "$libdeps/ArduinoJson"

# PlatformIO's registry normally supplies this metadata. Arduino upstream's
# package.json is versioned 2.0.17; PlatformIO repackages it as 3.20017.0.
python3 - "$work" <<'PY'
import json, os, sys
w=sys.argv[1]
def save(path, obj):
    with open(os.path.join(w,path),'w') as f: json.dump(obj,f,indent=2); f.write('\n')
def piopm(path, typ, name, version, owner, ident=None):
    spec={'owner':owner,'name':name,'requirements':None,'uri':None}
    if ident is not None: spec['id']=ident
    save(path+'/.piopm',{'type':typ,'name':name,'version':version,'spec':spec})
p='core/packages/framework-arduinoespressif32/package.json'
with open(os.path.join(w,p)) as f: framework=json.load(f)
assert framework['version']=='2.0.17'
framework['version']='3.20017.0'; save(p,framework)
piopm('core/platforms/espressif32','platform','espressif32','6.9.0','platformio',8026)
piopm('core/packages/framework-arduinoespressif32','tool','framework-arduinoespressif32','3.20017.0','platformio',8070)
for name,version,ident in [('tool-scons','4.40801.0',8192),('tool-esptoolpy','1.40501.0',8161)]:
    path='core/packages/'+name
    save(path+'/package.json',{'name':name,'version':version})
    piopm(path,'tool',name,version,'platformio',ident)
for owner,name,version in [('adafruit','Adafruit NeoPixel','1.12.3'),('bblanchon','ArduinoJson','7.2.1')]:
    piopm('project/.pio/libdeps/esp32/'+name,'library',name,version,owner)
PY

# No network, no host home/credentials, readonly toolchain/framework, no retained
# capabilities, and a copied project. Namespace isolation is defense in depth,
# not a substitute for independently verifying the compiler's provenance.
unshare -Urnm --fork bash -euc '
  work=$1; root=$work/root
  mount --make-rprivate /; mount -t tmpfs -o size=128m tmpfs "$root"
  mkdir -p "$root"/{usr,dev,core,project,python,tmp,home,proc,etc}
  mount --rbind /usr "$root/usr"; mount -o remount,bind,ro "$root/usr"
  ln -s usr/bin "$root/bin"; ln -s usr/lib "$root/lib"; ln -s usr/lib64 "$root/lib64"
  touch "$root/dev/null" "$root/dev/urandom"
  mount --bind /dev/null "$root/dev/null"; mount --bind /dev/urandom "$root/dev/urandom"
  mount --bind "$work/core" "$root/core"
  mount --bind "$work/core/packages" "$root/core/packages"; mount -o remount,bind,ro "$root/core/packages"
  mount --bind "$work/core/platforms" "$root/core/platforms"; mount -o remount,bind,ro "$root/core/platforms"
  mount --bind "$work/project" "$root/project"
  mount --bind "$work/python" "$root/python"; mount -o remount,bind,ro "$root/python"
  /usr/sbin/chroot "$root" /usr/bin/setpriv --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs \
    /usr/bin/env -i HOME=/home PATH=/usr/bin:/bin LANG=C.UTF-8 PLATFORMIO_CORE_DIR=/core \
    PLATFORMIO_SETTING_ENABLE_TELEMETRY=no PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=/python \
    python3 -m platformio run -d /project -e esp32 -j 2
' bash "$work"
elf="$work/project/.pio/build/esp32/firmware.elf"
readelf -h "$elf" | grep -E 'Class:|Machine:|Entry point address:'
readelf -h "$elf" | grep -q 'Machine:.*Tensilica Xtensa' || { echo 'Not an Xtensa ELF' >&2; exit 1; }
sha256sum "$work/project/.pio/build/esp32/firmware.bin"

if (( emulate )); then
  echo 'Fetching pinned esp32-emu 1.2.0 for a limited runtime smoke test...'
  mkdir -p "$work/emu"
  (cd "$work/emu" && npm pack esp32-emu@1.2.0 --silent >/dev/null)
  (cd "$work/emu" && echo 'c6a0ae5681c1dd9b3ff73d24254cfb4f79c9317ed9b8cfaf2a7ea3c21d46ea14071571d90863eba378d07315b9c73e401ae0d91b4de2cf9299b9b49972bfe3ef  esp32-emu-1.2.0.tgz' | sha512sum -c)
  tar --no-same-owner --no-same-permissions -xzf "$work/emu/esp32-emu-1.2.0.tgz" -C "$work/emu"
  cp "$work/project/tools/sandbox_esp32_smoke.mjs" "$work/emu/run.mjs"
  # Capture the emulator's verbose diagnostics; only summarize successful runs.
  if timeout 60 unshare -Urnm --fork bash -euc '
    work=$1; node=$2; root=$work/root
    mount --make-rprivate /; mount -t tmpfs -o size=128m tmpfs "$root"
    mkdir -p "$root"/{usr,dev,emu,build,tmp,home}
    mount --rbind /usr "$root/usr"; mount -o remount,bind,ro "$root/usr"
    ln -s usr/bin "$root/bin"; ln -s usr/lib "$root/lib"; ln -s usr/lib64 "$root/lib64"
    touch "$root/dev/null" "$root/dev/urandom" "$root/boot_app0.bin"
    mount --bind /dev/null "$root/dev/null"; mount --bind /dev/urandom "$root/dev/urandom"
    mount --bind "$work/emu" "$root/emu"; mount -o remount,bind,ro "$root/emu"
    mount --bind "$work/project/.pio/build/esp32" "$root/build"; mount -o remount,bind,ro "$root/build"
    mount --bind "$work/core/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" "$root/boot_app0.bin"
    mount -o remount,bind,ro "$root/boot_app0.bin"
    /usr/sbin/chroot "$root" /usr/bin/setpriv --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs \
      /usr/bin/env -i HOME=/home PATH=/usr/bin:/bin "$node" /emu/run.mjs
  ' bash "$work" "$node" >"$work/emulator.log" 2>&1; then
    grep -E 'SMOKE TEST PASSED|Provision: WIFI|Invalid Wi-Fi credentials' "$work/emulator.log"
  else
    echo 'Emulator smoke test failed; final diagnostics:' >&2
    tail -80 "$work/emulator.log" >&2
    exit 1
  fi
fi
