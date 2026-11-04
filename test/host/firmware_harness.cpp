// Host integration harness for the wall-clock firmware (src/main.cpp).
//
// WHAT THIS IS
//   src/main.cpp is compiled *as is* (via the include below) against host
//   shims for Arduino, WebServer, Preferences, WiFi, esp_timer and
//   Adafruit_NeoPixel, with the real ArduinoJson library. The harness then
//   drives the firmware the way the device would: it dispatches HTTP requests
//   into the registered routes, feeds serial bytes, toggles the button GPIO,
//   moves a virtual clock and captures every frame passed to strip.show().
//
// WHY IT EXISTS
//   Most of the firmware's risk lives in wiring that no native unit test can
//   reach: request validation, save-before-apply ordering, NVS fallback,
//   render cadence, LED index bounds. None of it needs hardware to check.
//
// WHAT IT DOES NOT PROVE
//   Nothing about the ESP32 target: no xtensa build, no newlib TZ quirks, no
//   real heap behaviour, no RMT/WS2812 timing, no Wi-Fi stack, no electrical
//   behaviour. It is not a substitute for WC-T-015/WC-T-016 or any hardware
//   acceptance test.
//
// BUILD: see run_host_tests.sh or test/host/CMakeLists.txt
//
// The firmware's engine helpers (wallclock::scale/mapPosition/hourPosition)
// are used to *compute expectations* here, because this file tests the
// firmware's wiring, not the engine maths. Engine maths is covered
// independently by engine_properties.cpp.

#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include "../../src/main.cpp"  // firmware under test: defines setup(), loop(), config, strip, server, prefs, oscillator

#include <cstdio>
#include <string>
#include <vector>

// Test hooks provided by stubs/host_stubs.cpp
extern time_t fakeEpoch;
extern int64_t fakeUs;
void hostSetPin(int pin, int level);
int hostPinMode(int pin);

// ---------------------------------------------------------------------------
// Minimal check framework: counts checks, prints only what fails.
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_failures = 0;

static void check(bool ok, const std::string& what = "condition") {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    printf("    FAIL  %s\n", what.c_str());
  }
}

#define CHECK(cond) check((cond), std::string(#cond))

static void checkEq(long long actual, long long expected, const std::string& what) {
  ++g_checks;
  if (actual != expected) {
    ++g_failures;
    printf("    FAIL  %s (expected %lld, got %lld)\n", what.c_str(), expected, actual);
  }
}

static void note(const std::string& what) { printf("    note  %s\n", what.c_str()); }

// ---------------------------------------------------------------------------
// Firmware control helpers
// ---------------------------------------------------------------------------

// Run the firmware's loop() for `ms` milliseconds (loop() ends in delay(1),
// which advances the virtual clock by 1 ms).
static void advance(int ms) {
  for (int i = 0; i < ms; ++i) loop();
}

// Simulate a cold boot: RAM state is re-initialised, then setup() runs with
// whatever is currently in the simulated NVS.
static void coldBoot() {
  config = Config();
  oscillator = Oscillator();
  strip.reset();
  timeAcquired.store(false);
  configurationNeedsRepair=false;
  revisionCounter=0;
  serialOverflow=false;
  cancelSerialConfig();
  server.routes.clear();
  server.started = false;
  server.handleCalls = 0;
  previousUs = (uint64_t)fakeUs;
  lastFrame = 0;
  rawButton = stableButton = false;
  buttonChanged = 0;
  serialLine = "";
  Serial.out.clear();
  Adafruit_NeoPixel::reset();
  WiFi.state = WL_IDLE_STATUS;
  WiFi.beginCount = 0;
  WiFi.lastSsid.clear();
  WiFi.lastPassword.clear();
  setup();
}

static HostHttpResponse request(const char* path, int method, const std::string& body = "",
                                const std::string& contentType = "application/json", const std::string& ifMatch = "") {
  for (auto& route : server.routes) {
    if (route.path == path && route.method == method) {
      server.req = HostHttpRequest();
      server.req.path = path;
      server.req.method = method;
      server.req.body = body;
      if (!contentType.empty()) server.req.headers["Content-Type"] = contentType;
      if (!ifMatch.empty()) server.req.headers["If-Match"] = ifMatch;
      server.res = HostHttpResponse();
      route.handler();
      return server.res;
    }
  }
  return HostHttpResponse();
}

static HostHttpResponse httpGet(const char* path) { return request(path, HTTP_GET); }

static HostHttpResponse httpPostConfig(const std::string& body, const std::string& contentType = "application/json") {
  const HostHttpResponse current=httpGet("/api/config");
  auto etag=current.headers.find("ETag");
  return request("/api/config", HTTP_POST, body, contentType, etag == current.headers.end() ? "" : etag->second);
}

// ---------------------------------------------------------------------------
// Configuration payload builder
//
// Mirrors the document the web UI submits: 13 members, colours as [r,g,b].
// ui_contract_test.mjs checks the UI really produces this shape; the optional
// `ui payload` scenario below feeds the UI's own bytes back into the firmware.
// ---------------------------------------------------------------------------
struct Payload {
  std::string count = "60";
  std::string offset = "0";
  std::string clockwise = "true";
  std::string idle = "false";
  std::string timezone = "UTC0";
  std::string period = "4.5";
  std::string low = "10";
  std::string high = "50";
  std::string am = "[255,100,20]";
  std::string pm = "[180,40,255]";
  std::string minute = "[20,150,255]";
  std::string second = "[255,30,40]";
  std::string background = "[255,255,255]";

  std::string json() const {
    return "{\"count\":" + count + ",\"offset\":" + offset + ",\"clockwise\":" + clockwise +
           ",\"timezone\":\"" + timezone + "\",\"period\":" + period + ",\"low\":" + low +
           ",\"high\":" + high + ",\"idle\":" + idle + ",\"am\":" + am + ",\"pm\":" + pm +
           ",\"minute\":" + minute + ",\"second\":" + second + ",\"background\":" + background + "}";
  }
};

// Canonical valid payload with a few fields replaced.
static std::string withCount(int n) {
  Payload p;
  p.count = std::to_string(n);
  return p.json();
}

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------

static bool sameColor(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// Frame bytes are stored GRB, exactly as a WS2812B would receive them.
static Color framePixel(const std::vector<uint8_t>& frame, int index) {
  if (index < 0 || (size_t)index * 3 + 2 >= frame.size()) return Color{1, 2, 3};  // sentinel: never matches
  return Color{frame[index * 3 + 1], frame[index * 3], frame[index * 3 + 2]};
}

// Returns the brightness percentage that reproduces `observed`, or -1.
// Written as a search so the harness does not need to know how the firmware
// derives modulation values; it only asserts that the result is one of the
// values the configured level range can produce.
static int brightnessOf(Color color, Color observed) {
  for (int b = 0; b <= 100; ++b)
    if (sameColor(scale(color, b), observed)) return b;
  return -1;
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

static bool hasRoute(const char* path, int method) {
  for (auto& route : server.routes)
    if (route.path == path && route.method == method) return true;
  return false;
}

static void scenario_boot() {
  prefs.store.kv.clear();
  fakeEpoch = 0;
  coldBoot();

  CHECK(hasRoute("/", HTTP_GET));
  CHECK(hasRoute("/api/config", HTTP_GET));
  CHECK(hasRoute("/api/status", HTTP_GET));
  CHECK(hasRoute("/api/config", HTTP_POST));
  check(!server.started, "legacy unauthenticated HTTP listener is not started");
  check(Serial.out.find("Web configuration disabled") != std::string::npos,
        "boot reports web configuration unavailable");
  checkEq(config.count, 60, "default pixel count");
  checkEq((long long)(config.period * 10), 45, "default modulation period is 4.5 s");
  checkEq((long long)config.low, 10, "default low brightness");
  checkEq((long long)config.high, 50, "default high brightness");
  CHECK(!config.idle);
  CHECK(!config.timezone.empty());
  checkEq(strip->numLEDs, config.count, "strip allocated to the configured count");
  CHECK(WiFi.status() != WL_CONNECTED);  // no credentials stored yet
  check(Serial.out.find("Provision") != std::string::npos, "boot banner documents serial provisioning");
}

static void scenario_config_api() {
  coldBoot();

  HostHttpResponse r = httpGet("/api/config");
  checkEq(r.code, 200, "GET /api/config status");
  CHECK(r.contentType == "application/json");
  {
    JsonDocument doc;
    CHECK(!deserializeJson(doc, r.body.c_str()));
    // The UI submits exactly these 13 members; changing the document shape means
    // updating include/web_ui.h and this harness together.
    checkEq(doc.size(), 13, "GET /api/config returns 13 members");
    checkEq(doc["count"].as<int>(), config.count, "count reflected");
  }

  // The UI reloads the document and posts it back unchanged; that must be accepted.
  checkEq(httpPostConfig(r.body).code, 200, "POST of the server's own document is accepted");

  Payload p;
  p.count = "144";
  p.offset = "7";
  p.clockwise = "false";
  p.period = "3";
  p.low = "5";
  p.high = "70";
  p.idle = "true";
  p.timezone = "EST5EDT,M3.2.0/2,M11.1.0/2";
  p.second = "[0,255,0]";
  checkEq(httpPostConfig(p.json()).code, 200, "valid payload accepted");
  checkEq(config.count, 144, "count applied");
  checkEq(config.offset, 7, "offset applied");
  CHECK(!config.clockwise);
  checkEq((long long)(config.period * 10), 30, "period applied");
  checkEq((long long)config.low, 5, "low applied");
  checkEq((long long)config.high, 70, "high applied");
  CHECK(config.idle);
  CHECK(config.timezone == "EST5EDT,M3.2.0/2,M11.1.0/2");
  CHECK(sameColor(config.second, Color{0, 255, 0}));
  checkEq(strip->numLEDs, 144, "strip resized to the new count");

  r = httpGet("/api/config");
  {
    JsonDocument doc;
    CHECK(!deserializeJson(doc, r.body.c_str()));
    checkEq(doc["count"].as<int>(), 144, "GET reflects the applied count");
  }

  r = httpGet("/api/status");
  checkEq(r.code, 200, "GET /api/status status");
  {
    JsonDocument doc;
    CHECK(!deserializeJson(doc, r.body.c_str()));
    CHECK(doc["synchronized"].is<bool>());
    CHECK(doc["connected"].is<bool>());
    CHECK(doc["ledBufferAvailable"].is<bool>());
    CHECK(doc["ledBufferAvailable"].as<bool>());
  }
}

static void scenario_rejections() {
  coldBoot();
  Payload base;
  base.count = "60";
  base.period = "4.5";
  base.low = "10";
  base.high = "50";
  base.idle = "false";
  checkEq(httpPostConfig(base.json()).code, 200, "baseline payload accepted");
  const int count0 = config.count;
  const double period0 = config.period, low0 = config.low, high0 = config.high;

  auto expect400 = [&](const std::string& body, const char* what) {
    checkEq(httpPostConfig(body).code, 400, std::string("rejected: ") + what);
    checkEq(config.count, count0, std::string("state unchanged after: ") + what);
    checkEq((long long)(config.period * 10), (long long)(period0 * 10), std::string("period unchanged after: ") + what);
    checkEq((long long)config.low, (long long)low0, std::string("low unchanged after: ") + what);
    checkEq((long long)config.high, (long long)high0, std::string("high unchanged after: ") + what);
  };

  expect400("", "empty body");
  expect400("[]", "array instead of object");
  expect400("{\"count\":60}", "incomplete document");
  expect400(base.json().substr(0, base.json().size() - 1), "truncated JSON");
  {
    Payload p = base;
    p.count = "true";
    expect400(p.json(), "count as boolean");
    p = base;
    p.count = "\"60\"";
    expect400(p.json(), "count as string");
    p = base;
    p.count = "60.5";
    expect400(p.json(), "non-integer count");
    p = base;
    p.count = "0";
    expect400(p.json(), "count 0");
    p = base;
    p.count = "301";
    expect400(p.json(), "count above the documented maximum");
    p = base;
    p.clockwise = "1";
    expect400(p.json(), "clockwise as number, not boolean");
    p = base;
    p.idle = "1";
    expect400(p.json(), "idle as number, not boolean");
    p = base;
    p.offset = "60";
    expect400(p.json(), "offset equal to count");
    p = base;
    p.offset = "-1";
    expect400(p.json(), "negative offset");
    p = base;
    p.low = "50";
    expect400(p.json(), "low == high");
    p = base;
    p.high = "101";
    expect400(p.json(), "high above 100");
    p = base;
    p.period = "2.9";
    expect400(p.json(), "period below 3 s");
    p = base;
    p.period = "6.1";
    expect400(p.json(), "period above 6 s");
    p = base;
    p.period = "\"4.5\"";
    expect400(p.json(), "period as string");
    p = base;
    p.am = "\"255,100,20\"";
    expect400(p.json(), "colour as string");
    p = base;
    p.am = "[255,100]";
    expect400(p.json(), "colour with two channels");
    p = base;
    p.am = "[256,100,20]";
    expect400(p.json(), "colour channel above 255");
    p = base;
    p.am = "[-1,100,20]";
    expect400(p.json(), "negative colour channel");
    p = base;
    p.timezone = "";
    expect400(p.json(), "empty timezone");
    p.timezone = "Not/A/Zone";
    expect400(p.json(), "IANA-shaped invalid timezone");
    p.timezone = "UTC0\\u0000BAD";
    expect400(p.json(), "embedded NUL in timezone");
    p.timezone = "EST5EDT,M3.6.0/2,M11.1.0/2";
    expect400(p.json(), "invalid timezone transition");
  }
  {
    Payload p = base;
    std::string extra = p.json();
    extra.insert(extra.size() - 1, ",\"extra\":1");
    expect400(extra, "unknown extra member");
  }
  // Transport-level guards.
  checkEq(httpPostConfig(base.json(), "text/plain").code, 415, "non-JSON media type rejected");
  checkEq(httpPostConfig(base.json(), "").code, 415, "missing media type rejected");
  {
    std::string big = base.json();
    big += std::string(2100, ' ');
    checkEq(httpPostConfig(big).code, 413, "oversized body rejected");
  }
  checkEq(httpPostConfig(base.json(), "application/json; charset=utf-8").code, 200, "JSON media type parameters accepted");

  // NVS failure must not apply the candidate.
  Preferences::failWrites = true;
  {
    Payload p = base;
    p.count = "30";
    checkEq(httpPostConfig(p.json()).code, 500, "persistence failure surfaces as 500");
    checkEq(config.count, 60, "nothing applied when persistence fails");
  }
  Preferences::failWrites = false;
}

static void scenario_concurrency_and_allocation() {
  prefs.store.kv.clear();
  coldBoot();
  const std::string body=withCount(30);
  const HostHttpResponse initial=httpGet("/api/config");
  const std::string tag=initial.headers.at("ETag");
  check(!tag.empty(), "config GET supplies revision");
  checkEq(request("/api/config",HTTP_POST,body,"application/json").code,428,"missing If-Match rejected");
  checkEq(config.count,60,"missing precondition leaves config unchanged");

  hostSetPin(BUTTON_PIN, LOW); advance(100);
  hostSetPin(BUTTON_PIN, HIGH); advance(100);
  CHECK(config.idle);
  const std::string storedAfterButton=prefs.store.kv["config"];
  const HostHttpResponse stale=request("/api/config",HTTP_POST,body,"application/json",tag);
  checkEq(stale.code,412,"stale write rejected after button press");
  CHECK(config.idle && config.count==60);
  CHECK(prefs.store.kv["config"]==storedAfterButton);
  CHECK(httpGet("/api/config").headers.at("ETag")!=tag);

  const auto oldStrip=strip.get();
  const auto oldBytes=strip->getPixels();
  const auto oldCount=strip->numPixels();
  const auto oldShows=Adafruit_NeoPixel::shows;
  Adafruit_NeoPixel::failNextAlloc=true;
  checkEq(httpPostConfig(body).code,503,"buffer allocation failure reported");
  JsonDocument allocationStatus;
  CHECK(!deserializeJson(allocationStatus,httpGet("/api/status").body.c_str()));
  CHECK(allocationStatus["ledBufferAvailable"].as<bool>());
  CHECK(strip.get()==oldStrip && strip->getPixels()==oldBytes && strip->numPixels()==oldCount);
  CHECK(prefs.store.kv["config"]==storedAfterButton);
  checkEq(hostPinMode(LED_PIN),OUTPUT,"failed candidate leaves LED pin active");
  checkEq(Adafruit_NeoPixel::shows,oldShows,"allocation failure does not blank strip");

  // Non-geometry changes reuse the existing strip without blanking or allocating.
  Payload sameCount; sameCount.idle="true"; sameCount.low="11";
  Adafruit_NeoPixel::failNextAlloc=true;
  checkEq(httpPostConfig(sameCount.json()).code,200,"same-count update does not allocate");
  CHECK(strip.get()==oldStrip && strip->getPixels()==oldBytes);
  CHECK(Adafruit_NeoPixel::failNextAlloc);
  checkEq(Adafruit_NeoPixel::shows,oldShows,"same-count update does not blank strip");
  Adafruit_NeoPixel::failNextAlloc=false;

  const std::string beforeReboot=httpGet("/api/config").headers.at("ETag");
  coldBoot();
  checkEq(request("/api/config",HTTP_POST,body,"application/json",beforeReboot).code,412,"pre-boot revision rejected after reboot");
  CHECK(config.count==60 && config.idle);
}

static void scenario_boot_allocation_recovery() {
  prefs.store.kv.clear();
  fakeEpoch=1793448000;
  Adafruit_NeoPixel::failNextAlloc=true;
  coldBoot();
  CHECK(!strip);
  CHECK(Serial.out.find("LED buffer allocation failed")!=std::string::npos);
  auto status=[]() {
    JsonDocument doc;
    CHECK(!deserializeJson(doc,httpGet("/api/status").body.c_str()));
    return doc;
  };
  CHECK(status()["ledBufferAvailable"].is<bool>());
  CHECK(!status()["ledBufferAvailable"].as<bool>());
  hostTimeSync();
  CHECK(status()["synchronized"].as<bool>());
  CHECK(!status()["ledBufferAvailable"].as<bool>());
  Adafruit_NeoPixel::reset(); advance(50);
  CHECK(Adafruit_NeoPixel::frames.empty());

  Adafruit_NeoPixel::failNextAlloc=true;
  checkEq(httpPostConfig(Payload{}.json()).code,503,"retry allocation failure reported");
  CHECK(!status()["ledBufferAvailable"].as<bool>());
  Preferences::failWrites=true;
  checkEq(httpPostConfig(Payload{}.json()).code,500,"persistence failure does not install replacement");
  Preferences::failWrites=false;
  CHECK(!strip && !status()["ledBufferAvailable"].as<bool>());
  checkEq(httpPostConfig(Payload{}.json()).code,200,"save recovers boot allocation failure");
  CHECK(strip && status()["ledBufferAvailable"].as<bool>());
  Adafruit_NeoPixel::reset(); advance(50);
  bool lit=false;
  for(const auto& frame:Adafruit_NeoPixel::frames) for(uint8_t value:frame) lit |= value!=0;
  CHECK(lit);
}

static void scenario_sync_status() {
  prefs.store.kv.clear(); fakeEpoch=1793448000; coldBoot();
  auto synced=[]() {
    JsonDocument d;
    auto response=httpGet("/api/status");
    if(deserializeJson(d,response.body.c_str())) return false;
    return d["synchronized"].as<bool>();
  };
  CHECK(!synced());
  Adafruit_NeoPixel::reset(); advance(50);
  for(const auto& frame:Adafruit_NeoPixel::frames)
    for(uint8_t value:frame) check(value==0,"plausible epoch without SNTP stays dark");
  hostTimeSync(); CHECK(synced());
  Adafruit_NeoPixel::reset(); advance(50);
  bool lit=false;
  for(const auto& frame:Adafruit_NeoPixel::frames) for(uint8_t value:frame) lit |= value!=0;
  CHECK(lit);
  coldBoot(); CHECK(!synced());
}

static void scenario_persistence() {
  prefs.store.kv.clear();
  coldBoot();
  checkEq(httpPostConfig(withCount(96)).code, 200, "save accepted");
  CHECK(prefs.store.kv["config"].find("\"count\":96") != std::string::npos);

  // Cold boot with valid NVS restores the configuration.
  coldBoot();
  checkEq(config.count, 96, "configuration restored after cold boot");
  checkEq(strip->numLEDs, 96, "strip resized from persisted configuration");

  // Corrupt NVS must fall back to defaults rather than to garbage/partial state.
  prefs.store.kv["config"] = "{\"count\":96}";
  coldBoot();
  checkEq(config.count, 60, "corrupt persisted configuration loads recovery defaults");
  CHECK(configurationNeedsRepair);
  checkEq((long long)(config.period * 10), 45, "defaults restored after corrupt NVS");
}

static void scenario_invalid_timezone_recovery() {
  prefs.store.kv.clear();
  Payload bad; bad.timezone="Not/A/Zone";
  prefs.store.kv["config"]=bad.json();
  fakeEpoch=1793448000; coldBoot(); hostTimeSync();
  CHECK(configurationNeedsRepair);
  const std::string original=prefs.store.kv["config"];
  JsonDocument status;
  CHECK(!deserializeJson(status,httpGet("/api/status").body.c_str()));
  CHECK(status["configurationNeedsRepair"].as<bool>());
  Adafruit_NeoPixel::reset(); advance(100);
  bool dark=true;
  for(const auto& frame:Adafruit_NeoPixel::frames) for(uint8_t pixel:frame) dark &= pixel==0;
  CHECK(dark);
  hostSetPin(BUTTON_PIN,LOW); advance(100); hostSetPin(BUTTON_PIN,HIGH); advance(100);
  CHECK(configurationNeedsRepair && prefs.store.kv["config"]==original);
  Preferences::failWrites=true;
  checkEq(httpPostConfig(Payload{}.json()).code,500,"failed repair remains disabled");
  Preferences::failWrites=false;
  CHECK(configurationNeedsRepair && prefs.store.kv["config"]==original);
  checkEq(httpPostConfig(Payload{}.json()).code,200,"valid configuration repairs stored fault");
  CHECK(!configurationNeedsRepair);
  Adafruit_NeoPixel::reset(); advance(50);
  bool lit=false; for(const auto& frame:Adafruit_NeoPixel::frames) for(uint8_t pixel:frame) lit |= pixel!=0;
  CHECK(lit);
  coldBoot(); CHECK(!configurationNeedsRepair);
  prefs.store.kv["config"]=""; coldBoot(); CHECK(configurationNeedsRepair);
  prefs.store.kv.clear(); Preferences::failBegin=true; coldBoot();
  CHECK(configurationNeedsRepair);
  checkEq(httpPostConfig(Payload{}.json()).code,500,"unavailable NVS cannot complete recovery");
  CHECK(configurationNeedsRepair);
  Preferences::failBegin=false;
}

static void scenario_display() {
  prefs.store.kv.clear();
  fakeEpoch = 0;
  coldBoot();

  // Before a plausible epoch the ring must stay dark (documented behaviour).
  Adafruit_NeoPixel::reset();
  advance(200);
  CHECK(Adafruit_NeoPixel::shows > 0);
  bool allDark = true;
  for (auto& frame : Adafruit_NeoPixel::frames)
    for (uint8_t byte : frame) allDark = allDark && byte == 0;
  CHECK(allDark);

  // A synchronized clock renders at a steady cadence.
  const time_t epoch = 1793448000;  // 2026-10-31T12:00:00Z
  fakeEpoch = epoch;
  hostTimeSync();
  struct tm local;
  localtime_r(&epoch, &local);

  Adafruit_NeoPixel::reset();
  advance(1000);
  const long frames = Adafruit_NeoPixel::shows;
  if (frames < 40) printf("    frame rate was %ld/s\n", frames);
  check(frames >= 40, "renders at least 40 frames per second (WC-SYS-060)");
  CHECK(Adafruit_NeoPixel::violations.empty());
  for (auto& frame : Adafruit_NeoPixel::frames) checkEq((long long)frame.size(), config.count * 3LL, "frame size matches pixel count");
  if (frames == 0) return;

  // Pixel semantics of the most recent frame.
  const Config cfg = config;
  const int hIndex = mapPosition(hourPosition({local.tm_hour, local.tm_min, local.tm_sec}), cfg);
  const int mIndex = mapPosition(local.tm_min, cfg);
  const int sIndex = mapPosition(local.tm_sec, cfg);
  const std::vector<uint8_t> last = Adafruit_NeoPixel::frames.back();

  const Color secondSeen = framePixel(last, sIndex);
  CHECK(sameColor(secondSeen, scale(cfg.second, cfg.high)));  // WC-SYS-039/040

  if (hIndex != sIndex && hIndex != mIndex) {
    const Color hourColor = local.tm_hour < 12 ? cfg.am : cfg.pm;
    const int b = brightnessOf(hourColor, framePixel(last, hIndex));
    check(b >= 0, "hour pixel is the configured hour colour (WC-SYS-021/023)");
    if (b >= 0) check(b >= cfg.low && b <= cfg.high, "hour brightness within [low, high] (WC-SYS-033)");
  }
  if (mIndex != sIndex && mIndex != hIndex) {
    const int b = brightnessOf(cfg.minute, framePixel(last, mIndex));
    check(b >= 0, "minute pixel is the configured minute colour (WC-SYS-020)");
    if (b >= 0) check(b >= cfg.low && b <= cfg.high, "minute brightness within [low, high] (WC-SYS-033)");
  }
  if (!cfg.idle) {
    for (int i = 0; i < cfg.count; ++i) {
      if (i == hIndex || i == mIndex || i == sIndex) continue;
      check(sameColor(framePixel(last, i), Color{0, 0, 0}));
    }
  }

  // Modulation must actually move (WC-SYS-030/037): half a period apart the hour
  // pixel must differ, and wall-clock changes must not reset the oscillator.
  if (hIndex != sIndex && hIndex != mIndex) {
    const double phaseBefore = oscillator.phase();
    const Color hourBefore = framePixel(Adafruit_NeoPixel::frames.back(), hIndex);
    advance(2250);
    const Color hourLater = framePixel(Adafruit_NeoPixel::frames.back(), hIndex);
    check(!sameColor(hourBefore, hourLater) || cfg.high - cfg.low < 2.0,
          "hour brightness is modulated over time (WC-SYS-030)");
    fakeEpoch += 11;  // a wall-clock correction
    advance(20);
    check(oscillator.phase() > phaseBefore, "wall-clock changes do not reset modulation phase (WC-SYS-062)");
  }

  // LED index bounds across the documented count range (WC-SYS-019).
  for (int n : {1, 2, 60, 144, 300}) {
    checkEq(httpPostConfig(withCount(n)).code, 200, "count " + std::to_string(n) + " accepted");
    Adafruit_NeoPixel::reset();
    advance(60);
    checkEq(strip->numLEDs, n, "strip resized for count " + std::to_string(n));
    CHECK(Adafruit_NeoPixel::violations.empty());
    CHECK(!Adafruit_NeoPixel::frames.empty());
    for (auto& frame : Adafruit_NeoPixel::frames) checkEq((long long)frame.size(), n * 3LL, "frame size for count " + std::to_string(n));
  }

  // With a single pixel every indication collides; seconds must win (WC-SYS-040).
  checkEq(httpPostConfig(withCount(1)).code, 200, "count 1 accepted");
  advance(60);
  if (!Adafruit_NeoPixel::frames.empty()) {
    const Config single = config;
    CHECK(sameColor(framePixel(Adafruit_NeoPixel::frames.back(), 0), scale(single.second, single.high)));
  }
}

static void scenario_button() {
  prefs.store.kv.clear();
  coldBoot();
  const bool idle0 = config.idle;

  hostSetPin(BUTTON_PIN, LOW);
  advance(100);
  check(config.idle != idle0, "a press toggles idle illumination (WC-SYS-025)");
  const bool afterFirst = config.idle;
  advance(400);  // hold
  check(config.idle == afterFirst, "holding the button does not repeat the toggle");
  CHECK(prefs.store.kv["config"].find("\"idle\":") != std::string::npos);

  hostSetPin(BUTTON_PIN, HIGH);
  advance(100);
  check(config.idle == afterFirst, "releasing the button does not toggle again");

  hostSetPin(BUTTON_PIN, LOW);
  advance(100);
  check(config.idle != afterFirst, "a second press toggles back");
  hostSetPin(BUTTON_PIN, HIGH);
  advance(50);
}

static void scenario_serial() {
  prefs.store.kv.clear();
  coldBoot();

  // Over-long line must be discarded without wedging the parser.
  Serial.in.assign(2000, 'x');
  Serial.in.push_back('\n');
  advance(5);
  checkEq(WiFi.beginCount, 0, "garbage serial input does not provision Wi-Fi");

  const std::string attack=std::string(161,'x')+"WIFI Evil\tsecretpass\n";
  Serial.in.assign(attack.begin(),attack.end()); advance(5);
  checkEq(WiFi.beginCount,0,"suffix of oversized serial line is not interpreted as a command");

  const std::string line = "WIFI TestNet\tsecretpass\n";
  Serial.in.assign(line.begin(), line.end());
  advance(5);
  JsonDocument credentials;
  CHECK(!deserializeJson(credentials,prefs.store.kv["wifi"]));
  CHECK(credentials["ssid"] == "TestNet");
  CHECK(credentials["password"] == "secretpass");
  checkEq(WiFi.beginCount, 1, "credentials trigger Wi-Fi association");
  check(!server.started, "Wi-Fi provisioning does not enable insecure HTTP");
  checkEq(server.handleCalls, 0, "legacy HTTP server is not polled during runtime");
  CHECK(WiFi.lastSsid == "TestNet");
  CHECK(WiFi.lastPassword == "secretpass");
  const std::string saved=prefs.store.kv["wifi"];
  Preferences::failWrites=true;
  const std::string replacement="WIFI Other\tanotherpass\n";
  Serial.in.assign(replacement.begin(),replacement.end()); advance(5);
  Preferences::failWrites=false;
  CHECK(prefs.store.kv["wifi"]==saved);
  checkEq(WiFi.beginCount,1,"failed credential write does not reconnect");
  coldBoot();
  CHECK(WiFi.lastSsid=="TestNet" && WiFi.lastPassword=="secretpass");
  check(!server.started, "restart with saved credentials does not enable insecure HTTP");
}

static void scenario_serial_configuration() {
  prefs.store.kv.clear();
  coldBoot();
  auto line = [](const std::string& text) {
    Serial.out.clear();
    Serial.in.assign(text.begin(),text.end()); Serial.in.push_back('\n');
    advance(1);
    return Serial.out;
  };
  auto begin = [&](const std::string& body) {
    check(line("CONFIG BEGIN " + std::to_string(body.size())).find("CONFIG READY") != std::string::npos,
          "serial config begins");
  };
  auto append = [&](const std::string& body) {
    for(size_t pos=0;pos<body.size();pos+=80) {
      check(line("CONFIG CHUNK " + std::to_string(pos) + " " + body.substr(pos,80)).find("CONFIG CHUNK OK") != std::string::npos,
            "serial config chunk acknowledged");
    }
  };
  const std::string body=withCount(30);
  const std::string current=line("CONFIG GET");
  check(current.find("CONFIG JSON ")==0,"serial configuration can be read");
  JsonDocument readBack;
  CHECK(!deserializeJson(readBack,current.substr(12)));
  checkEq(readBack["count"].as<int>(),60,"serial read returns current count");
  check(line("CONFIG BEGIN 2049").find("CONFIG ERR invalid length") != std::string::npos,"oversized configuration rejected at begin");
  check(line("CONFIG BEGIN 0").find("CONFIG ERR invalid length") != std::string::npos,"empty configuration rejected at begin");
  begin(body); append(body);
  check(line("CONFIG COMMIT").find("CONFIG SAVED") != std::string::npos,"valid serial configuration saved");
  checkEq(config.count,30,"serial configuration applied");
  check(!server.started,"serial configuration does not enable web listener");
  coldBoot();
  checkEq(config.count,30,"serial configuration survives simulated restart");
  const auto stored=prefs.store.kv["config"];

  check(line("CONFIG CHUNK 0 x").find("CONFIG ERR not started") != std::string::npos,"chunk without begin rejected");
  begin(body);
  check(line("CONFIG CHUNK 1 x").find("CONFIG ERR invalid offset") != std::string::npos,"wrong chunk offset aborts");
  check(line("CONFIG COMMIT").find("CONFIG ERR not started") != std::string::npos,"aborted transaction cannot commit");
  begin(body);
  check(line("CONFIG CHUNK 0 " + body.substr(0,20)).find("CONFIG CHUNK OK") != std::string::npos,"partial chunk accepted");
  check(line("CONFIG COMMIT").find("CONFIG ERR incomplete") != std::string::npos,"incomplete transfer rejected");
  CHECK(prefs.store.kv["config"]==stored && config.count==30);
  begin(body);
  check(line("CONFIG CHUNK 0 " + body.substr(0,20)).find("CONFIG CHUNK OK") != std::string::npos,"first chunk accepted");
  check(line("CONFIG CHUNK 0 " + body.substr(0,20)).find("CONFIG ERR invalid offset") != std::string::npos,"duplicate chunk rejected");
  begin(body);
  check(line("CONFIG CHUNK 0 \t").find("CONFIG ERR invalid or oversized chunk") != std::string::npos,"control bytes rejected");
  begin(body);
  check(line("CONFIG CANCEL").find("CONFIG CANCELED") != std::string::npos,"serial config can be canceled");
  check(line("CONFIG COMMIT").find("CONFIG ERR not started") != std::string::npos,"canceled transaction cannot commit");

  begin(body); append(body);
  Preferences::failWrites=true;
  check(line("CONFIG COMMIT").find("CONFIG ERR persistence") != std::string::npos,"serial write failure reported");
  Preferences::failWrites=false;
  CHECK(prefs.store.kv["config"]==stored && config.count==30);

  const std::string larger=withCount(40);
  begin(larger); append(larger);
  Adafruit_NeoPixel::failNextAlloc=true;
  check(line("CONFIG COMMIT").find("CONFIG ERR LED buffer allocation") != std::string::npos,"serial allocation failure reported");
  CHECK(prefs.store.kv["config"]==stored && config.count==30);
  Adafruit_NeoPixel::failNextAlloc=false;

  const std::string invalid="{}";
  begin(invalid); append(invalid);
  check(line("CONFIG COMMIT").find("CONFIG ERR invalid settings") != std::string::npos,"invalid settings rejected over serial");
  CHECK(prefs.store.kv["config"]==stored && config.count==30);

  begin(larger); append(larger);
  hostSetPin(BUTTON_PIN, LOW); advance(100); hostSetPin(BUTTON_PIN, HIGH); advance(100);
  check(line("CONFIG COMMIT").find("CONFIG ERR settings changed") != std::string::npos,"button change rejects stale serial update");
  CHECK(config.idle && config.count==30);
  const auto afterButton=prefs.store.kv["config"];
  begin(larger);
  check(line(std::string(161,'x')).find("Serial line too long") != std::string::npos,"oversized line reported");
  check(line("CONFIG COMMIT").find("CONFIG ERR not started") != std::string::npos,"oversized line aborts transaction");
  CHECK(prefs.store.kv["config"]==afterButton && config.count==30);
  begin(larger); append(larger); coldBoot();
  check(line("CONFIG COMMIT").find("CONFIG ERR not started") != std::string::npos,"restart aborts unfinished serial update");
  CHECK(prefs.store.kv["config"]==afterButton && config.count==30);

  prefs.store.kv["config"]="corrupt"; coldBoot();
  CHECK(configurationNeedsRepair);
  begin(body); append(body);
  check(line("CONFIG COMMIT").find("CONFIG SAVED") != std::string::npos,"serial save repairs corrupt stored config");
  CHECK(!configurationNeedsRepair && config.count==30);
  coldBoot();
  CHECK(!configurationNeedsRepair && config.count==30);
}

// Optional: POST the exact bytes the browser UI produces (written by
// ui_contract_test.mjs). Only runs when a path is supplied.
static void scenario_ui_payload(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    note(std::string("UI payload not available at ") + path);
    return;
  }
  std::string body;
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
  fclose(f);

  coldBoot();
  const HostHttpResponse r = httpPostConfig(body);
  checkEq(r.code, 200, "the web UI's own POST body is accepted by the firmware");
  if (r.code != 200) printf("    body was: %s\n", body.c_str());
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  const char* uiPayloadPath = argc > 1 ? argv[1] : nullptr;

  struct Scenario {
    const char* name;
    void (*fn)();
  };
  const Scenario scenarios[] = {
      {"boot defaults and route registration", scenario_boot},
      {"configuration API round-trip", scenario_config_api},
      {"invalid configuration rejection", scenario_rejections},
      {"conditional updates and allocation failures", scenario_concurrency_and_allocation},
      {"boot LED buffer failure and recovery", scenario_boot_allocation_recovery},
      {"SNTP synchronization status", scenario_sync_status},
      {"persistence, cold boot and corrupt NVS", scenario_persistence},
      {"invalid stored timezone recovery", scenario_invalid_timezone_recovery},
      {"LED output, cadence and bounds", scenario_display},
      {"physical button", scenario_button},
      {"serial provisioning", scenario_serial},
      {"serial configuration and recovery", scenario_serial_configuration},
  };

  printf("firmware_host_harness: %zu scenarios\n", sizeof scenarios / sizeof scenarios[0]);
  for (const Scenario& s : scenarios) {
    const int before = g_failures;
    s.fn();
    printf("  %s  %s\n", g_failures == before ? "PASS" : "FAIL", s.name);
  }
  if (uiPayloadPath) {
    const int before = g_failures;
    scenario_ui_payload(uiPayloadPath);
    printf("  %s  web UI payload accepted\n", g_failures == before ? "PASS" : "FAIL");
  }

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
