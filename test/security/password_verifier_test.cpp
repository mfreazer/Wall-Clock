#include "password_verifier.h"
#include "basic_auth.h"
#include <cstdio>
#include <string>

using namespace wallclock::security;
static int checks = 0, failures = 0;
static void check(bool value, const char* message) {
  ++checks;
  if (!value) { ++failures; std::printf("FAIL %s\n", message); }
}

int main() {
  std::array<uint8_t, kSaltBytes> salt{};
  for (size_t i = 0; i < salt.size(); ++i) salt[i] = static_cast<uint8_t>(i);
  // Expected value independently computed with Python hashlib.pbkdf2_hmac.
  const std::array<uint8_t, kVerifierBytes> expected = {
    0xd9,0xf9,0x5f,0x65,0xc2,0xdf,0x9d,0x28,0x5d,0x26,0x88,0x23,0x00,0xca,0x5b,0xe2,
    0x9e,0x3e,0xd5,0x00,0x55,0x66,0x63,0x83,0x5c,0x4c,0x62,0xe2,0x70,0x51,0x50,0x22
  };
  const IterationLimits limits{2, 10000};
  PasswordVerifier stored;
  check(makePasswordVerifier("correct horse battery staple", salt, 10000, limits, stored), "derive verifier");
  check(stored.digest == expected, "PBKDF2-HMAC-SHA256 known answer");
  check(stored.salt == salt && stored.iterations == 10000, "salt and iterations preserved");
  check(verifyPassword("correct horse battery staple", stored, limits) == VerifyResult::Match, "correct password");
  check(verifyPassword("wrong password", stored, limits) == VerifyResult::Mismatch, "wrong password");
  const std::string correct = "Basic YWRtaW46Y29ycmVjdCBob3JzZSBiYXR0ZXJ5IHN0YXBsZQ==";
  check(verifyBasicAuthorization(correct, "admin", stored, limits) == AuthorizationResult::Authorized, "valid Basic credentials");
  check(verifyBasicAuthorization("bAsIc YWRtaW46Y29ycmVjdCBob3JzZSBiYXR0ZXJ5IHN0YXBsZQ==", "admin", stored, limits) == AuthorizationResult::Authorized, "scheme is case-insensitive");
  check(verifyBasicAuthorization("Basic YWRtaW46d3JvbmcgcGFzc3dvcmQ=", "admin", stored, limits) == AuthorizationResult::Denied, "wrong Basic password denied");
  check(verifyBasicAuthorization("Basic Z3Vlc3Q6Y29ycmVjdCBob3JzZSBiYXR0ZXJ5IHN0YXBsZQ==", "admin", stored, limits) == AuthorizationResult::Denied, "wrong Basic user denied");
  check(verifyBasicAuthorization("Basic YWRtaW46", "admin", stored, limits) == AuthorizationResult::Denied, "empty Basic password denied");
  check(verifyBasicAuthorization("", "admin", stored, limits) == AuthorizationResult::Denied, "missing header denied");
  check(verifyBasicAuthorization("Bearer token", "admin", stored, limits) == AuthorizationResult::Denied, "wrong auth scheme denied");
  check(verifyBasicAuthorization("Basic YWRt:aW4=", "admin", stored, limits) == AuthorizationResult::Denied, "invalid base64 denied");
  check(verifyBasicAuthorization("Basic Y=WRtaW4=", "admin", stored, limits) == AuthorizationResult::Denied, "misplaced padding denied");
  check(verifyBasicAuthorization("Basic YWRtaW46\n", "admin", stored, limits) == AuthorizationResult::Denied, "whitespace in base64 denied");
  check(verifyBasicAuthorization("Basic " + std::string(400, 'A'), "admin", stored, limits) == AuthorizationResult::Denied, "oversized header denied");
  check(verifyBasicAuthorization(correct, "ad:min", stored, limits) == AuthorizationResult::Error, "invalid configured user fails closed");
  check(verifyBasicAuthorization(correct, "admin", stored, {0, 10000}) == AuthorizationResult::Error, "invalid auth policy fails closed");
  auto damaged = stored; damaged.digest[31] ^= 1;
  check(verifyPassword("correct horse battery staple", damaged, limits) == VerifyResult::Mismatch, "tampered digest");
  damaged = stored; damaged.salt[0] ^= 1;
  check(verifyPassword("correct horse battery staple", damaged, limits) == VerifyResult::Mismatch, "tampered salt");
  damaged = stored; damaged.iterations = 0;
  check(verifyPassword("correct horse battery staple", damaged, limits) == VerifyResult::Error, "invalid iteration count");
  check(verifyPassword("", stored, limits) == VerifyResult::Error, "empty password fails closed");
  check(verifyPassword(std::string(kMaxPasswordBytes + 1, 'x'), stored, limits) == VerifyResult::Error, "oversized password fails closed");
  PasswordVerifier unchanged = stored;
  check(!makePasswordVerifier("", salt, 10000, limits, unchanged), "reject empty password");
  check(unchanged.digest == stored.digest && unchanged.salt == stored.salt && unchanged.iterations == stored.iterations,
        "failed creation leaves existing verifier unchanged");
  check(!makePasswordVerifier("test", salt, 0, limits, unchanged), "reject zero iterations");
  check(!makePasswordVerifier("test", salt, 10001, limits, unchanged), "reject iterations above maximum");
  check(verifyPassword("test", stored, {10001, 20000}) == VerifyResult::Error, "reject iterations below minimum");
  check(verifyPassword("test", stored, {10000, 9999}) == VerifyResult::Error, "reject inverted limits");
  check(verifyPassword("test", stored, {0, 10000}) == VerifyResult::Error, "reject zero minimum");
  check(!makePasswordVerifier(std::string(kMaxPasswordBytes + 1, 'x'), salt, 10000, limits, unchanged), "reject oversized password");
  const std::string embeddedNul("a\0b", 3);
  for (size_t i = 0; i < salt.size(); ++i) salt[i] = static_cast<uint8_t>(i + 16);
  const std::array<uint8_t, kVerifierBytes> binaryExpected = {
    0x25,0x8d,0xfc,0x35,0x66,0x97,0x5f,0x81,0xa3,0xcc,0x86,0xfd,0x85,0x0d,0x99,0xac,
    0x43,0xc4,0x13,0xd6,0xd4,0x8a,0xf1,0x7d,0xe5,0x70,0x4a,0xdc,0xdc,0x75,0xe5,0xe0
  };
  check(makePasswordVerifier(embeddedNul, salt, 2, limits, damaged), "derive password with embedded NUL");
  check(damaged.digest == binaryExpected, "embedded NUL known answer");
  check(verifyPassword(embeddedNul, damaged, limits) == VerifyResult::Match, "embedded NUL round-trip");
  check(verifyPassword("a", damaged, limits) == VerifyResult::Mismatch, "NUL is not truncated");
  std::printf("password verifier: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
