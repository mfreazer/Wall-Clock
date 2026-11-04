#include "password_verifier.h"

// Mbed TLS 2.28's internal constant_time.h does not declare C++ linkage.
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>

namespace wallclock::security {
namespace {
bool derive(std::string_view password, const std::array<uint8_t, kSaltBytes>& salt,
            uint32_t iterations, IterationLimits limits, std::array<uint8_t, kVerifierBytes>& digest) {
  if (password.empty() || password.size() > kMaxPasswordBytes || limits.minimum == 0 ||
      limits.maximum < limits.minimum || iterations < limits.minimum || iterations > limits.maximum) return false;
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  int result = sha256 ? mbedtls_md_setup(&context, sha256, 1) : -1;
  if (result == 0) {
    result = mbedtls_pkcs5_pbkdf2_hmac(&context,
        reinterpret_cast<const unsigned char*>(password.data()), password.size(),
        salt.data(), salt.size(), iterations, digest.size(), digest.data());
  }
  mbedtls_md_free(&context);
  if (result != 0) mbedtls_platform_zeroize(digest.data(), digest.size());
  return result == 0;
}
}  // namespace

bool makePasswordVerifier(std::string_view password,
                          const std::array<uint8_t, kSaltBytes>& salt,
                          uint32_t iterations, IterationLimits limits, PasswordVerifier& output) {
  PasswordVerifier candidate{};
  candidate.salt = salt;
  candidate.iterations = iterations;
  if (!derive(password, salt, iterations, limits, candidate.digest)) {
    mbedtls_platform_zeroize(candidate.digest.data(), candidate.digest.size());
    return false;
  }
  output = candidate;
  mbedtls_platform_zeroize(candidate.digest.data(), candidate.digest.size());
  return true;
}

VerifyResult verifyPassword(std::string_view password, const PasswordVerifier& stored, IterationLimits limits) {
  std::array<uint8_t, kVerifierBytes> candidate{};
  if (!derive(password, stored.salt, stored.iterations, limits, candidate)) {
    mbedtls_platform_zeroize(candidate.data(), candidate.size());
    return VerifyResult::Error;
  }
  const bool same = mbedtls_ct_memcmp(candidate.data(), stored.digest.data(), candidate.size()) == 0;
  mbedtls_platform_zeroize(candidate.data(), candidate.size());
  return same ? VerifyResult::Match : VerifyResult::Mismatch;
}
}  // namespace wallclock::security
