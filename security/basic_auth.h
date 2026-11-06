// Basic authentication helper for a verified TLS connection only.
// Not connected to the clock. It does not provide TLS, rate limiting or CSRF protection.
#pragma once
#include "password_verifier.h"
#include <string_view>

namespace wallclock::security {
enum class AuthorizationResult { Authorized, Denied, Error };

// expectedUser is chosen by the integrating application. The caller must reject
// duplicate/missing/oversized headers and use only verified TLS. No secrets may
// be logged. An Error must be treated as a denial, not an authentication bypass.
AuthorizationResult verifyBasicAuthorization(std::string_view authorization,
    std::string_view expectedUser, const PasswordVerifier& stored, IterationLimits limits);
}
