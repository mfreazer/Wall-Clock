#!/usr/bin/env bash
# Exercise the not-yet-integrated password verifier using pinned upstream Mbed TLS.
set -euo pipefail
if [[ $# != 0 ]]; then echo "Usage: $0" >&2; exit 2; fi
for tool in git make g++; do command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }; done
repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
git -c advice.detachedHead=false clone --quiet --depth 1 --branch v2.28.7 https://github.com/Mbed-TLS/mbedtls "$work/mbedtls"
[[ $(git -C "$work/mbedtls" rev-parse HEAD) == 555f84735aecdbd76a566cf087ec8425dfb0c8ab ]] || { echo 'Unexpected Mbed TLS commit' >&2; exit 1; }
if ! make -s -C "$work/mbedtls/library" -j2 libmbedcrypto.a > "$work/build.log" 2>&1; then
  echo 'Mbed TLS build failed:' >&2; tail -40 "$work/build.log" >&2; exit 1
fi
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/security" -isystem "$work/mbedtls/include" \
  "$repo/security/password_verifier.cpp" "$repo/security/basic_auth.cpp" "$repo/test/security/password_verifier_test.cpp" \
  "$work/mbedtls/library/libmbedcrypto.a" -o "$work/password_verifier_test"
"$work/password_verifier_test"
