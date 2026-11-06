// Password-verifier building block. Not yet connected to the clock or its UI.
// Security depends on TLS, fresh random salt, access policy and provisioning.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wallclock::security {
constexpr size_t kSaltBytes = 16;
constexpr size_t kVerifierBytes = 32;
constexpr size_t kMaxPasswordBytes = 128;

// A caller must choose an acceptable range and enforce it on both create and
// load. No production cost has yet been selected or benchmarked on target.
struct IterationLimits { uint32_t minimum; uint32_t maximum; };

// The iteration count is recorded with the verifier.
struct PasswordVerifier {
  uint32_t iterations = 0;
  std::array<uint8_t, kSaltBytes> salt{};
  std::array<uint8_t, kVerifierBytes> digest{};
};

enum class VerifyResult { Match, Mismatch, Error };

// The caller must obtain a unique salt from a cryptographically secure random
// generator. The password may contain NUL bytes and must not be logged.
bool makePasswordVerifier(std::string_view password,
                          const std::array<uint8_t, kSaltBytes>& salt,
                          uint32_t iterations, IterationLimits limits, PasswordVerifier& output);
VerifyResult verifyPassword(std::string_view password, const PasswordVerifier& stored,
                            IterationLimits limits);
}  // namespace wallclock::security
