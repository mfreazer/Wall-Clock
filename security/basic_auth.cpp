#include "basic_auth.h"
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <array>

namespace wallclock::security {
namespace {
constexpr size_t kMaxUserBytes = 32;
constexpr size_t kMaxDecoded = kMaxUserBytes + 1 + kMaxPasswordBytes;
constexpr size_t kMaxEncoded = ((kMaxDecoded + 2) / 3) * 4;

struct SensitiveBuffer {
  std::array<unsigned char, kMaxDecoded> bytes{};
  ~SensitiveBuffer() { mbedtls_platform_zeroize(bytes.data(), bytes.size()); }
};
bool isBase64(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}
}  // namespace

AuthorizationResult verifyBasicAuthorization(std::string_view authorization,
    std::string_view expectedUser, const PasswordVerifier& stored, IterationLimits limits) {
  if (expectedUser.empty() || expectedUser.size() > kMaxUserBytes ||
      expectedUser.find(':') != std::string_view::npos ||
      expectedUser.find('\0') != std::string_view::npos) return AuthorizationResult::Error;
  // HTTP auth scheme tokens are case-insensitive. Require a single ASCII space.
  constexpr std::string_view scheme = "basic ";
  if (authorization.size() <= scheme.size()) return AuthorizationResult::Denied;
  for (size_t i = 0; i < scheme.size(); ++i) {
    char c = authorization[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != scheme[i]) return AuthorizationResult::Denied;
  }
  authorization.remove_prefix(scheme.size());
  if (authorization.empty() || authorization.size() > kMaxEncoded || authorization.size() % 4 != 0)
    return AuthorizationResult::Denied;
  for (size_t i = 0; i < authorization.size(); ++i) {
    const char c = authorization[i];
    if (!isBase64(c) || (c == '=' && i < authorization.size() - 2) ||
        (i > 0 && authorization[i - 1] == '=' && c != '=')) return AuthorizationResult::Denied;
  }
  SensitiveBuffer decoded;
  size_t length = 0;
  if (mbedtls_base64_decode(decoded.bytes.data(), decoded.bytes.size(), &length,
       reinterpret_cast<const unsigned char*>(authorization.data()), authorization.size()) != 0)
    return AuthorizationResult::Denied;
  size_t separator = 0;
  while (separator < length && decoded.bytes[separator] != ':') ++separator;
  if (separator != expectedUser.size() || separator == length ||
      mbedtls_ct_memcmp(decoded.bytes.data(), expectedUser.data(), expectedUser.size()) != 0)
    return AuthorizationResult::Denied;
  const auto password = std::string_view(reinterpret_cast<const char*>(decoded.bytes.data() + separator + 1),
                                         length - separator - 1);
  if (password.empty()) return AuthorizationResult::Denied;
  const auto result = verifyPassword(password, stored, limits);
  if (result == VerifyResult::Error) return AuthorizationResult::Error;
  return result == VerifyResult::Match ? AuthorizationResult::Authorized : AuthorizationResult::Denied;
}
}  // namespace wallclock::security
