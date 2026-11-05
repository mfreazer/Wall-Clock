#!/usr/bin/env bash
# Host-only shared-network access gate. Expected to fail until auth is implemented.
set -euo pipefail
if [[ $# != 0 ]]; then echo "Usage: $0" >&2; exit 2; fi
for tool in git g++; do command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }; done
repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
if [[ -n ${ARDUINOJSON_SRC:-} ]]; then
  if [[ -f "$ARDUINOJSON_SRC/src/ArduinoJson.h" ]]; then json_src=$ARDUINOJSON_SRC/src
  else json_src=$ARDUINOJSON_SRC; fi
  [[ -f "$json_src/ArduinoJson.h" ]] || { echo 'ARDUINOJSON_SRC does not contain ArduinoJson.h' >&2; exit 1; }
  echo 'Using caller-supplied ArduinoJson; its identity is not verified.'
else
  git clone --quiet --depth 1 --branch v7.2.1 https://github.com/bblanchon/ArduinoJson "$work/ArduinoJson" 2>/dev/null
  [[ $(git -C "$work/ArduinoJson" rev-parse HEAD) == f9fe8557f13d8949eafd49ebd93a2929c4e5065f ]] || { echo 'Unexpected ArduinoJson revision' >&2; exit 1; }
  json_src="$work/ArduinoJson/src"
fi
g++ -std=gnu++17 -Wall -Wextra -O0 -g -I"$repo/test/host/stubs" -I"$repo/include" -isystem "$json_src" \
  "$repo/test/security/access_gate.cpp" "$repo/test/host/stubs/host_stubs.cpp" -o "$work/access_gate"
echo 'Host-only handler authorization gate (direct dispatch bypasses the disabled listener; does not test TLS or networking):'
"$work/access_gate"
