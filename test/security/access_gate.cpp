// Shared-network release gate: this intentionally fails until unauthorized
// callers cannot read protected APIs or change configuration. Host-only; it
// does not prove TLS, browser behavior, or physical security.
#define main firmware_harness_main
#include "../host/firmware_harness.cpp"
#undef main

static bool denied(const HostHttpResponse& response) {
  return response.sent && (response.code == 401 || response.code == 403);
}

int main() {
  prefs.store.kv.clear();
  coldBoot();

  const auto status = httpGet("/api/status");
  check(denied(status), "unauthenticated status read denied (401 or 403)");
  const auto settings = httpGet("/api/config");
  check(denied(settings), "unauthenticated settings read denied (401 or 403)");

  // Give the unauthenticated caller an otherwise valid concurrency token. A
  // rejected stale/missing If-Match must never be confused with authorization.
  const std::string validRevision = configRevision().s;
  const auto before = prefs.store.kv;
  const int originalCount = config.count;
  Payload replacement;
  replacement.count = "61";
  const auto write = request("/api/config", HTTP_POST, replacement.json(),
                             "application/json", validRevision);
  check(denied(write), "unauthenticated write denied even with current ETag");
  check(config.count == originalCount, "unauthenticated write cannot change active configuration");
  check(prefs.store.kv == before, "unauthenticated write cannot change persisted configuration");

  printf("shared-network access gate: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
