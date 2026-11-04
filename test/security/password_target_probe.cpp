// Test-only Xtensa probe. Not the firmware, and never to be flashed.
#include <Arduino.h>
#include "password_verifier.h"
#include "basic_auth.h"
using namespace wallclock::security;

void setup() {
  Serial.begin(115200);
  unsigned checks = 0, failures = 0;
  auto check = [&](bool condition) { ++checks; if (!condition) ++failures; };
  const IterationLimits limits{2, 10}; // Small test vectors only, not a security policy.
  std::array<uint8_t, kSaltBytes> salt{};
  for (size_t i = 0; i < salt.size(); ++i) salt[i] = static_cast<uint8_t>(i + 16);
  const std::array<uint8_t, kVerifierBytes> expected = {
    0x25,0x8d,0xfc,0x35,0x66,0x97,0x5f,0x81,0xa3,0xcc,0x86,0xfd,0x85,0x0d,0x99,0xac,
    0x43,0xc4,0x13,0xd6,0xd4,0x8a,0xf1,0x7d,0xe5,0x70,0x4a,0xdc,0xdc,0x75,0xe5,0xe0
  };
  const std::string_view password("a\0b", 3);
  PasswordVerifier value;
  check(makePasswordVerifier(password, salt, 2, limits, value));
  check(value.digest == expected);
  check(verifyPassword(password, value, limits) == VerifyResult::Match);
  check(verifyPassword("a", value, limits) == VerifyResult::Mismatch);
  check(verifyBasicAuthorization("Basic YWRtaW46YQBi", "admin", value, limits) == AuthorizationResult::Authorized);
  check(verifyBasicAuthorization("Basic YWRtaW46YQ==", "admin", value, limits) == AuthorizationResult::Denied);
  check(verifyBasicAuthorization("Basic YWRtaW46YQBi", "guest", value, limits) == AuthorizationResult::Denied);
  value.digest[0] ^= 1;
  check(verifyPassword(password, value, limits) == VerifyResult::Mismatch);
  check(verifyPassword(password, value, {3, 10}) == VerifyResult::Error);
  if (failures) Serial.printf("PASSWORD_TARGET_FAIL %u/%u\n", failures, checks);
  else Serial.printf("PASSWORD_TARGET_PASS %u\n", checks);
}
void loop() { delay(1000); }
