// Independent probes of the USB configuration protocol covering cases the
// shipped harness does not assert.
#include "src/main.cpp"
#include <cstdio>
#include <cstring>
extern time_t fakeEpoch; extern int64_t fakeUs;
static void coldBoot() {
  config = Config(); oscillator = Oscillator(); strip.reset();
  timeAcquired.store(false); configurationNeedsRepair=false; revisionCounter=0;
  serialOverflow=false; cancelSerialConfig(); server.routes.clear();
  server.started=false; server.handleCalls=0; previousUs=(uint64_t)fakeUs;
  lastFrame=0; rawButton=stableButton=false; buttonChanged=0;
  serialLine=""; Serial.out.clear(); Serial.in.clear(); Adafruit_NeoPixel::reset();
  WiFi.state=WL_IDLE_STATUS; WiFi.beginCount=0;
  WiFi.lastSsid.clear(); WiFi.lastPassword.clear(); setup();
}
static std::string line(const std::string& text) {
  Serial.out.clear();
  Serial.in.assign(text.begin(), text.end()); Serial.in.push_back('\n');
  for (int i=0;i<1;++i) loop();
  return Serial.out;
}static std::string cfg(int count, const char* tz) {
  JsonDocument d; encode(d, config);
  d["count"]=count; d["timezone"]=tz;
  String s; serializeJson(d,s); return std::string(s.c_str());
}
static bool send(const std::string& body) {
  if (line("CONFIG BEGIN "+std::to_string(body.size())).find("CONFIG READY")==std::string::npos) return false;
  for (size_t p=0;p<body.size();p+=80)
    if (line("CONFIG CHUNK "+std::to_string(p)+" "+body.substr(p,80)).find("CONFIG CHUNK OK")==std::string::npos) return false;
  return line("CONFIG COMMIT").find("CONFIG SAVED")!=std::string::npos;
}
int main(){
  prefs.store.kv.clear(); fakeEpoch=1793448000; coldBoot();
  int fails=0;
  auto check=[&](bool ok,const char* what){ printf("%s  %s\n", ok?"PASS":"FAIL", what); if(!ok) ++fails; };

  // 1. A committed time zone really reaches libc, not just the stored struct.
  tzset(); const std::string before = tzname[0] ? tzname[0] : "";
  check(send(cfg(60,"EST5EDT,M3.2.0/2,M11.1.0/2")), "commit a DST rule over USB");
  tzset(); const std::string after = tzname[0] ? tzname[0] : "";
  check(before != after && after == "EST", ("libc picks up the new zone (" + before + " -> " + after + ")").c_str());

  // 2. A blank line mid-transfer aborts it (documented interruption rule).
  line("CONFIG BEGIN 100");
  check(line("").find("CONFIG ERR interrupted")!=std::string::npos, "blank line aborts a transfer");
  check(line("CONFIG COMMIT").find("CONFIG ERR not started")!=std::string::npos, "aborted transfer cannot commit");
  check(config.count==60, "aborted transfer left the configuration alone");

  // 3. A non-CONFIG command mid-transfer aborts it but still runs.
  line("CONFIG BEGIN 100");
  const auto mixed = line("WIFI SomeNet\tpassword1");
  check(mixed.find("CONFIG ERR interrupted")!=std::string::npos && mixed.find("Wi-Fi credentials saved")!=std::string::npos,
        "interleaved WIFI aborts the transfer and still runs");
  check(line("CONFIG COMMIT").find("CONFIG ERR not started")!=std::string::npos, "interleaved transfer cannot commit");

  // 4. PING mid-transfer does not disturb it (a client may keep probing).
  const std::string body = cfg(72,"UTC0");
  line("CONFIG BEGIN "+std::to_string(body.size()));
  line("CONFIG CHUNK 0 "+body.substr(0,40));
  check(line("CONFIG PING").find("CONFIG PONG")!=std::string::npos, "ping answered mid-transfer");
  bool rest=true;
  for (size_t p=40;p<body.size();p+=80)
    rest = rest && line("CONFIG CHUNK "+std::to_string(p)+" "+body.substr(p,80)).find("CONFIG CHUNK OK")!=std::string::npos;
  check(rest && line("CONFIG COMMIT").find("CONFIG SAVED")!=std::string::npos, "transfer survives an interleaved ping");
  check(config.count==72, "interleaved-ping transfer applied");

  // 5. A chunk longer than the declared length is rejected.
  line("CONFIG BEGIN 10");
  check(line("CONFIG CHUNK 0 0123456789ABCDEF").find("CONFIG ERR invalid or oversized chunk")!=std::string::npos,
        "chunk past the declared length rejected");
  check(line("CONFIG COMMIT").find("CONFIG ERR not started")!=std::string::npos, "oversized chunk aborts");

  // 6. Non-ASCII / high bytes are rejected.
  line("CONFIG BEGIN 100"); line("CONFIG CHUNK 0 abc");
  check(line("CONFIG CHUNK 3 \xc3\xa9").find("CONFIG ERR invalid or oversized chunk")!=std::string::npos,
        "non-ASCII chunk rejected");
  check(line("CONFIG COMMIT").find("CONFIG ERR not started")!=std::string::npos, "non-ASCII chunk aborts");

  // 7. Bad time zone over USB is rejected and nothing changes.
  const auto storedBefore = prefs.store.kv;
  const std::string bad = cfg(72,"Not/A/Zone");
  line("CONFIG BEGIN "+std::to_string(bad.size()));
  bool chunksOk=true;
  for (size_t p=0;p<bad.size();p+=80)
    chunksOk = chunksOk && line("CONFIG CHUNK "+std::to_string(p)+" "+bad.substr(p,80)).find("CONFIG CHUNK OK")!=std::string::npos;
  const auto verdict = line("CONFIG COMMIT");
  printf("    note  chunking ok=%d commit said: %s\n", (int)chunksOk, verdict.c_str());
  check(verdict.find("CONFIG ERR invalid settings")!=std::string::npos, "unsupported time zone rejected");
  check(prefs.store.kv==storedBefore && config.count==72, "rejected update changed nothing");

  // 8. The web listener stays closed throughout.
  check(!server.started && server.handleCalls==0, "web listener never started or polled");
  printf("\n%d failures\n", fails);
  return fails?1:0;
}
