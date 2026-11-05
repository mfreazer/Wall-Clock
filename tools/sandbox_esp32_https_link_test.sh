#!/usr/bin/env bash
# Compile/link-only HTTPS server feasibility probe. Test-only: never flash.
set -euo pipefail
if [[ ${1:-} == --help || $# -lt 1 ]]; then
  echo "Usage: $0 <retained-build-directory> [--with-clock] [--keep]"
  echo 'First run ./tools/sandbox_esp32_build.sh --keep and pass its printed scratch directory.'
  [[ ${1:-} == --help ]] && exit 0 || exit 2
fi
bundle_arg=$1
shift
keep=0
with_clock=0
for arg in "$@"; do
  case "$arg" in
    --keep) keep=1 ;;
    --with-clock) with_clock=1 ;;
    *) echo "Unknown argument: $arg" >&2; exit 2 ;;
  esac
done
for cmd in python3 sha256sum unshare mount setpriv readelf; do
  command -v "$cmd" >/dev/null || { echo "Missing required command: $cmd" >&2; exit 1; }
done
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { echo 'Requires x86-64 Linux' >&2; exit 1; }
bundle=$(cd "$bundle_arg" && pwd -P)
for name in core/platforms/espressif32 core/packages/toolchain-xtensa-esp32 core/packages/framework-arduinoespressif32 python; do
  [[ -e "$bundle/$name" ]] || { echo "Missing retained dependency: $name" >&2; exit 1; }
done
# These hashes identify selected files, not the entire bundle or official provenance.
(cd "$bundle/core/packages/toolchain-xtensa-esp32" && sha256sum -c <<'HASHES'
52dca444cca7be0e980060969d820f0223b3baa880dd798287111aabc08b4caa  bin/xtensa-esp32-elf-gcc
3f5a4f3495f7c1ad8aee81e8157d88ffed37044fc61c8097589462be6777ba14  bin/xtensa-esp32-elf-as
84bf2c7d64a5574f308b873799492fc8c7e39c05d41e5ca295b5799d122477f4  bin/xtensa-esp32-elf-ld
71a3df1b39ed6382f48ae82e589a8fb01caf70002d18b51591c06ca5ec469c1f  bin/xtensa-esp32-elf-nm
f2c595c463fc0de4fdf77863f86298d5aa530442b8f887c129f912222c0da3e4  libexec/gcc/xtensa-esp32-elf/8.4.0/cc1
c733360a497f2b3b500f1b203197f2428e43ac84bea451f9f675a43cbf3efe68  libexec/gcc/xtensa-esp32-elf/8.4.0/cc1plus
HASHES
)
python3 - "$bundle" <<'PYCODE'
import json, os, sys
root = sys.argv[1]
for path, expected in [('core/platforms/espressif32/platform.json', '6.9.0'),
                       ('core/packages/framework-arduinoespressif32/package.json', '3.20017.0'),
                       ('core/packages/toolchain-xtensa-esp32/package.json', '8.4.0+2021r2-patch5')]:
    with open(os.path.join(root, path)) as f:
        actual = json.load(f)['version']
    if actual != expected:
        raise SystemExit(f'Unexpected version in {path}: {actual}')
PYCODE
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$HOME/.cache"
work=$(mktemp -d "$HOME/.cache/wallclock-esp32-https-XXXXXXXX")
cleanup() {
  if (( keep )); then echo "Probe scratch retained: $work"; else rm -rf -- "$work"; fi
}
trap cleanup EXIT
mkdir -p "$work"/{root,core/packages,core/platforms,project/src}
if (( with_clock )); then
  # Reuse only a bundle prepared for the same dependency specification.
  cmp -s "$repo/platformio.ini" "$bundle/project/platformio.ini" || { echo 'Bundle platformio.ini does not match current project' >&2; exit 1; }
  [[ -d "$bundle/project/.pio/libdeps/esp32" ]] || { echo 'Bundle lacks project library dependencies' >&2; exit 1; }
  cp "$repo/platformio.ini" "$work/project/"
  cp -a "$repo/src/." "$work/project/src/"
  [[ ! -e "$work/project/src/https_link_probe.cpp" ]] || { echo 'Probe filename already exists in clock source' >&2; exit 1; }
  cp -a "$repo/include" "$work/project/"
  [[ ! -d "$repo/lib" ]] || cp -a "$repo/lib" "$work/project/"
  mkdir -p "$work/project/.pio"
  cp -a "$bundle/project/.pio/libdeps" "$work/project/.pio/"
  { echo '#define WALLCLOCK_HTTPS_LINK_WITH_CLOCK 1'; cat "$repo/test/security/https_link_probe.cpp"; } > "$work/project/src/https_link_probe.cpp"
  # Patch the copied sketch only. This makes the linker retain the probe without
  # altering the repository or pretending that the resulting image is usable.
  python3 - "$work/project/src/main.cpp" <<'PYCODE'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
marker = 'void setup() {\n'
if text.count(marker) != 1:
    raise SystemExit('Could not uniquely locate clock setup()')
text = text.replace(marker, 'void httpsLinkProbe();\n' + marker + ' httpsLinkProbe();\n', 1)
path.write_text(text)
PYCODE
else
  cp "$repo/test/security/https_link_probe.cpp" "$work/project/src/main.cpp"
  cat > "$work/project/platformio.ini" <<'INI'
[env:esp32]
platform = espressif32@6.9.0
board = esp32dev
framework = arduino
build_flags = -std=gnu++17
build_unflags = -std=gnu++11
INI
fi
echo 'WARNING: unverified compiler. Compile/link probe only; never flash.'
if (( with_clock )); then echo 'Mode: clock sketch with an inserted HTTPS link probe (scratch copy only).'; fi
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
  echo 'HTTPS link probe build failed:' >&2; tail -80 "$work/build.log" >&2; exit 1
fi
tail -12 "$work/build.log"
elf="$work/project/.pio/build/esp32/firmware.elf"
readelf -h "$elf" | grep -q 'Machine:.*Tensilica Xtensa' || { echo 'Not an Xtensa ELF' >&2; exit 1; }
# Run the third-party nm in the same isolated style as the build. Do not execute
# an unverified compiler tool directly on the host.
if ! unshare -Urnm --fork bash -euc '
  work=$1; bundle=$2; root=$work/root
  mount --make-rprivate /; mount -t tmpfs -o size=128m tmpfs "$root"
  mkdir -p "$root"/{usr,tool,build,tmp,home}
  mount --rbind /usr "$root/usr"; mount -o remount,bind,ro "$root/usr"
  ln -s usr/bin "$root/bin"; ln -s usr/lib "$root/lib"; ln -s usr/lib64 "$root/lib64"
  mount --bind "$bundle/core/packages/toolchain-xtensa-esp32" "$root/tool"; mount -o remount,bind,ro "$root/tool"
  mount --bind "$work/project/.pio/build/esp32" "$root/build"; mount -o remount,bind,ro "$root/build"
  /usr/sbin/chroot "$root" /usr/bin/setpriv --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs \
    /usr/bin/env -i HOME=/home PATH=/usr/bin:/bin /tool/bin/xtensa-esp32-elf-nm /build/firmware.elf
' bash "$work" "$bundle" > "$work/symbols.txt"; then
  echo 'Symbol inspection failed' >&2; exit 1
fi
for symbol in httpd_ssl_start httpd_ssl_stop; do
  grep -Eq "[[:space:]]T[[:space:]]${symbol}$" "$work/symbols.txt" || { echo "Missing linked symbol: $symbol" >&2; exit 1; }
done
echo 'HTTPS LINK PROBE PASSED: Xtensa ELF contains httpd_ssl_start and httpd_ssl_stop.'
echo 'Compile/link evidence only: no TLS handshake, certificate, authentication, clock integration or hardware test.'
