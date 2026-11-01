#!/usr/bin/env bash
# Host-side verification for the wall-clock project.
#
# Runs every check that does not need an ESP32:
#
#   1. the project's own native tests (test/*.cpp), normally via CMake/CTest,
#      here also compiled directly so this script works with just a compiler;
#   2. the engine property suite (test/host/engine_properties.cpp);
#   3. the firmware integration harness (test/host/firmware_harness.cpp),
#      which compiles src/main.cpp against host shims and the real ArduinoJson,
#      then drives HTTP, NVS, the button, serial provisioning and the LED loop;
#   4. the browser UI contract test (test/host/ui_contract_test.mjs), which also
#      hands the UI's own POST body to the firmware harness.
#
# Steps whose inputs are not present (e.g. src/main.cpp before the firmware
# lands on a branch) are reported as SKIP, unless --require-firmware is given,
# in which case they are failures. That is what CI uses.
#
# Sanitizers are enabled automatically when the toolchain supports them.
#
# Usage: test/host/run_host_tests.sh [--root DIR] [--require-firmware]
#                                    [--no-sanitizers] [--keep]
#
# Exit status: 0 = every executed check passed, 1 = a check failed,
#              64 = bad usage.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REQUIRE_FIRMWARE=0
USE_SANITIZERS=auto
KEEP=0

usage() {
  sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case "$1" in
    --root) ROOT="$(cd "$2" && pwd)"; shift 2 ;;
    --require-firmware) REQUIRE_FIRMWARE=1; shift ;;
    --no-sanitizers) USE_SANITIZERS=off; shift ;;
    --keep) KEEP=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 64 ;;
  esac
done

CXX="${CXX:-g++}"
NODE="${NODE:-node}"
ARDUINOJSON_TAG="${ARDUINOJSON_TAG:-v7.2.1}"

WORK="$(mktemp -d)"
cleanup() {
  if [ "$KEEP" -eq 1 ]; then
    echo "artifacts kept in $WORK"
  else
    rm -rf "$WORK"
  fi
}
trap cleanup EXIT

PASSED=0
FAILED=0
SKIPPED=0

pass() { PASSED=$((PASSED + 1)); printf 'PASS  %s\n' "$1"; }
fail() {
  FAILED=$((FAILED + 1))
  printf 'FAIL  %s\n' "$1"
  shift
  for line in "$@"; do printf '      %s\n' "$line"; done
}
skip() {
  if [ "$REQUIRE_FIRMWARE" -eq 1 ]; then
    fail "$1" "$2 (required by --require-firmware)"
  else
    SKIPPED=$((SKIPPED + 1))
    printf 'SKIP  %s  (%s)\n' "$1" "$2"
  fi
}

show_log() { # show_log FILE [LINES]
  [ -s "$1" ] || return 0
  head -n "${2:-15}" "$1" | sed 's/^/      /'
}

# ---------------------------------------------------------------------------
# Toolchain probes
# ---------------------------------------------------------------------------
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "error: no C++ compiler ($CXX). Set CXX=... to point at one." >&2
  exit 64
fi

SAN_FLAGS=()
if [ "$USE_SANITIZERS" = "auto" ]; then
  printf 'int main(){return 0;}\n' > "$WORK/san_probe.cpp"
  if "$CXX" -fsanitize=address,undefined "$WORK/san_probe.cpp" -o "$WORK/san_probe" >/dev/null 2>&1 &&
     "$WORK/san_probe" >/dev/null 2>&1; then
    SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer)
    echo "sanitizers: address,undefined"
  else
    echo "sanitizers: unavailable, running without"
  fi
fi

BASE_FLAGS=(-std=c++17 -Wall -Wextra -O1 -g)
# -O0 for the firmware harness: ArduinoJson 7.2.1 templates trip GCC's
# -Wmaybe-uninitialized false positive under -O1, which would bury real warnings.
FW_FLAGS=(-std=gnu++17 -Wall -Wextra -O0 -g)

echo "root: $ROOT"
echo

# ---------------------------------------------------------------------------
# 1. The project's own native tests
# ---------------------------------------------------------------------------
run_native_tests() {
  local files=("$ROOT"/test/*.cpp)
  if [ ! -e "${files[0]}" ]; then
    skip "native tests (test/*.cpp)" "no test sources on this branch"
    return
  fi
  if [ ! -d "$ROOT/include" ]; then
    skip "native tests (test/*.cpp)" "no include/ directory"
    return
  fi

  local file name mode extra binary
  for file in "${files[@]}"; do
    name="$(basename "$file" .cpp)"
    for mode in default ndebug; do
      extra=()
      [ "$mode" = "ndebug" ] && extra=(-DNDEBUG) # Release semantics: assert-based tests must still be meaningful
      binary="$WORK/native-$name-$mode"
      if ! "$CXX" "${BASE_FLAGS[@]}" "${extra[@]}" -I"$ROOT/include" "$file" -o "$binary" > "$WORK/native-$name-$mode.log" 2>&1; then
        fail "native $name ($mode): compile" "$(show_log "$WORK/native-$name-$mode.log")"
        continue
      fi
      if "$binary" > "$WORK/native-$name-$mode.out" 2>&1; then
        pass "native $name ($mode)"
      else
        fail "native $name ($mode): run" "$(show_log "$WORK/native-$name-$mode.out")"
      fi
    done
  done
}

# ---------------------------------------------------------------------------
# 2. Engine property suite
# ---------------------------------------------------------------------------
run_engine_properties() {
  if [ ! -f "$ROOT/include/clock_engine.h" ]; then
    skip "engine properties" "include/clock_engine.h not present"
    return
  fi
  if ! "$CXX" "${BASE_FLAGS[@]}" "${SAN_FLAGS[@]}" -I"$ROOT/include" \
        "$ROOT/test/host/engine_properties.cpp" -o "$WORK/engine_properties" > "$WORK/props.log" 2>&1; then
    fail "engine properties: compile" "$(show_log "$WORK/props.log" 20)"
    return
  fi
  if "$WORK/engine_properties" > "$WORK/props.out" 2>&1; then
    pass "engine properties"
  else
    fail "engine properties: run" "$(grep -E 'FAIL|[0-9]+ failures' "$WORK/props.out" | head -n 20)"
  fi
}

# ---------------------------------------------------------------------------
# 3. Firmware integration harness
# ---------------------------------------------------------------------------
resolve_arduinojson() {
  local candidate
  # An explicit override wins, but only if it actually exists — otherwise fall
  # through to the cache locations and then to a clone.
  if [ -n "${ARDUINOJSON_SRC:-}" ] && [ -d "$ARDUINOJSON_SRC" ]; then
    if [ -d "$ARDUINOJSON_SRC/src" ]; then echo "$ARDUINOJSON_SRC/src"; else echo "$ARDUINOJSON_SRC"; fi
    return 0
  fi
  for candidate in "$ROOT/third_party/ArduinoJson/src" "$ROOT/.deps/ArduinoJson/src"; do
    [ -d "$candidate" ] && { echo "$candidate"; return 0; }
  done
  if command -v git >/dev/null 2>&1 && command -v "$CXX" >/dev/null 2>&1; then
    if git clone --quiet --depth 1 --branch "$ARDUINOJSON_TAG" \
         https://github.com/bblanchon/ArduinoJson "$WORK/ArduinoJson" >/dev/null 2>&1; then
      echo "$WORK/ArduinoJson/src"
      return 0
    fi
  fi
  return 1
}

run_firmware_harness() {
  if [ ! -f "$ROOT/src/main.cpp" ]; then
    skip "firmware harness" "src/main.cpp not present on this branch"
    return
  fi
  if [ ! -f "$ROOT/include/web_ui.h" ]; then
    skip "firmware harness" "include/web_ui.h not present"
    return
  fi

  local json_src
  if ! json_src="$(resolve_arduinojson)"; then
    skip "firmware harness" "ArduinoJson $ARDUINOJSON_TAG unavailable (set ARDUINOJSON_SRC to a local checkout)"
    return
  fi

  if ! "$CXX" "${FW_FLAGS[@]}" "${SAN_FLAGS[@]}" \
        -I"$ROOT/test/host/stubs" -I"$ROOT/include" -isystem "$json_src" \
        "$ROOT/test/host/firmware_harness.cpp" "$ROOT/test/host/stubs/host_stubs.cpp" \
        -o "$WORK/firmware_harness" > "$WORK/firmware.log" 2>&1; then
    fail "firmware harness: compile" "$(show_log "$WORK/firmware.log" 25)"
    return
  fi

  local ui_payload=()
  [ -s "$WORK/ui_post_body.json" ] && ui_payload=("$WORK/ui_post_body.json")

  if "$WORK/firmware_harness" "${ui_payload[@]}" > "$WORK/firmware.out" 2>&1; then
    pass "firmware harness"
    sed 's/^/      /' "$WORK/firmware.out"
  else
    fail "firmware harness: run" "$(grep -E 'FAIL|failures' "$WORK/firmware.out" | head -n 25)"
  fi
  if [ -s "$WORK/firmware.log" ]; then
    echo "      compiler warnings:"
    show_log "$WORK/firmware.log" 10
  fi
}

# ---------------------------------------------------------------------------
# 4. Browser UI contract test
# ---------------------------------------------------------------------------
run_ui_contract() {
  if [ ! -f "$ROOT/include/web_ui.h" ]; then
    skip "ui contract" "include/web_ui.h not present"
    return
  fi
  if ! command -v "$NODE" >/dev/null 2>&1; then
    skip "ui contract" "node not installed (install Node.js to run it)"
    return
  fi
  if "$NODE" "$ROOT/test/host/ui_contract_test.mjs" --root "$ROOT" --emit "$WORK/ui_post_body.json" \
       > "$WORK/ui.out" 2>&1; then
    pass "ui contract"
    tail -n 1 "$WORK/ui.out" | sed 's/^/      /'
  else
    fail "ui contract: run" "$(grep -E 'FAIL|error|Error' "$WORK/ui.out" | head -n 20)"
  fi
}

echo "== native project tests =="
run_native_tests
echo
echo "== engine properties =="
run_engine_properties
echo
echo "== browser UI contract =="
run_ui_contract
echo
echo "== firmware integration =="
run_firmware_harness
echo
printf '%s\n' "--------------------------------------------"
printf 'passed %d, failed %d, skipped %d\n' "$PASSED" "$FAILED" "$SKIPPED"
[ "$FAILED" -eq 0 ]
