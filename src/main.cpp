#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <atomic>
#include <memory>
#include <new>
#include <sys/time.h>
#include <time.h>
#include "clock_engine.h"
#include "web_ui.h"

// Provision through serial on first boot: WIFI <ssid>\t<password>
#ifndef LED_PIN
#define LED_PIN 18
#endif
#ifndef BUTTON_PIN
#define BUTTON_PIN 27
#endif
using namespace wallclock;
Config config;
Oscillator oscillator;
Preferences prefs;
WebServer server(80);
std::unique_ptr<Adafruit_NeoPixel> strip;
std::atomic<bool> timeAcquired{false};
String revisionEpoch;
uint32_t revisionCounter=0;
String configRevision() { return "\"" + revisionEpoch + "-" + String(revisionCounter) + "\""; }
void bumpRevision() {
 if (++revisionCounter==0) revisionEpoch=String(esp_random(),HEX)+"-"+String(esp_random(),HEX);
}
void onTimeSync(struct timeval*) { timeAcquired.store(true); }
uint64_t previousUs=0, lastFrame=0;
uint32_t buttonChanged=0;
bool rawButton=false, stableButton=false;
String serialLine;
bool serialOverflow=false;
bool configurationNeedsRepair=false;

void encode(JsonDocument& d,const Config& c) {
 d["count"]=c.count;d["offset"]=c.offset;d["clockwise"]=c.clockwise;d["idle"]=c.idle;
 d["period"]=c.period;d["low"]=c.low;d["high"]=c.high;d["timezone"]=c.timezone;
 const char* keys[]={"am","pm","minute","second","background"};
 Color colors[]={c.am,c.pm,c.minute,c.second,c.background};
 for(int i=0;i<5;++i){auto a=d[keys[i]].to<JsonArray>();a.add(colors[i].r);a.add(colors[i].g);a.add(colors[i].b);}
}
bool decode(const String& text,Config& c) {
 JsonDocument d;if(deserializeJson(d,text) || !d.is<JsonObject>())return false;
 // Require a complete, typed configuration; never coerce strings or wrap channels.
 if(!d["count"].is<int>() || !d["offset"].is<int>() || !d["clockwise"].is<bool>() || !d["idle"].is<bool>() || !d["timezone"].is<const char*>())return false;
 for(auto key:{"period","low","high"})if(!d[key].is<double>())return false;
 c.count=d["count"];c.offset=d["offset"];c.clockwise=d["clockwise"];c.idle=d["idle"];
 c.period=d["period"];c.low=d["low"];c.high=d["high"];c.timezone=d["timezone"].as<std::string>();
 const char* keys[]={"am","pm","minute","second","background"};Color* colors[]={&c.am,&c.pm,&c.minute,&c.second,&c.background};
 for(int i=0;i<5;++i){JsonArray a=d[keys[i]].as<JsonArray>();if(a.size()!=3)return false;for(auto v:a)if(!v.is<int>() || v.as<int>()<0 || v.as<int>()>255)return false;*colors[i]={a[0].as<uint8_t>(),a[1].as<uint8_t>(),a[2].as<uint8_t>()};}
 if(d.size()!=13)return false;
 return valid(c);
}
bool save(const Config& c) {JsonDocument d;encode(d,c);String text;serializeJson(d,text);return prefs.putString("config",text)==text.length();}
// Allocate the new buffer before saving or modifying the current strip. NeoPixel's
// updateLength() frees the old buffer even when allocation of the new one fails.
std::unique_ptr<Adafruit_NeoPixel> prepareStrip(const Config& c) {
 std::unique_ptr<Adafruit_NeoPixel> next(new(std::nothrow) Adafruit_NeoPixel(c.count,-1,NEO_GRB+NEO_KHZ800));
 if (!next || !next->getPixels() || next->numPixels()!=c.count) return nullptr;
 return next;
}
void applyStripConfiguration(const Config& c, std::unique_ptr<Adafruit_NeoPixel> next) {
 if (strip) { strip->clear(); strip->show(); }
 // Destroy the old driver before begin(): its destructor resets the output pin.
 strip.reset();
 strip=std::move(next); strip->setPin(LED_PIN); strip->begin(); strip->clear(); strip->show();
 config=c;
 setenv("TZ",c.timezone.c_str(),1);tzset();
}
// Store both credentials in a single NVS value so a failed update cannot mix
// a new SSID with the old password. Legacy keys are read only for migration.
bool wifiCredentials(const String& text, String& ssid, String& pass) {
 JsonDocument doc;
 if(deserializeJson(doc,text) || !doc["ssid"].is<const char*>() || !doc["password"].is<const char*>()) return false;
 ssid=doc["ssid"].as<String>(); pass=doc["password"].as<String>();
 return ssid.length()>0 && ssid.length()<=32 && pass.length()<=63 && (pass.length()==0 || pass.length()>=8);
}
void setup() {
 Serial.begin(115200);pinMode(BUTTON_PIN,INPUT_PULLUP);
 bool preferencesReady=prefs.begin("wallclock",false);
 Config loaded;
 if(!preferencesReady) configurationNeedsRepair=true;
 else if(prefs.isKey("config")) {
  if(decode(prefs.getString("config",""),loaded)) config=loaded;
  else configurationNeedsRepair=true;
 }
 if(configurationNeedsRepair) Serial.println("Stored configuration invalid or unavailable; display disabled until settings are saved");
 auto initialStrip=prepareStrip(config);
 if(initialStrip) applyStripConfiguration(config,std::move(initialStrip));
 else Serial.println("LED buffer allocation failed; display unavailable");
 WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
 revisionEpoch=String(esp_random(),HEX)+"-"+String(esp_random(),HEX);
 String ssid,pass; String storedWifi=prefs.getString("wifi","");
 if(storedWifi.length()) {
  if(!wifiCredentials(storedWifi,ssid,pass)) { ssid=""; pass=""; Serial.println("Invalid stored Wi-Fi credentials; reprovision over serial"); }
 } else { ssid=prefs.getString("ssid",""); pass=prefs.getString("password",""); }
 if(ssid.length())WiFi.begin(ssid.c_str(),pass.c_str());
 sntp_set_time_sync_notification_cb(onTimeSync);
 configTzTime(config.timezone.c_str(),"pool.ntp.org","time.nist.gov");
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",WEB_UI);});
 server.on("/api/config",HTTP_GET,[]{JsonDocument d;encode(d,config);String text;serializeJson(d,text);server.sendHeader("ETag",configRevision());server.send(200,"application/json",text);});
 server.on("/api/status",HTTP_GET,[]{JsonDocument d;d["connected"]=WiFi.status()==WL_CONNECTED;d["configurationNeedsRepair"]=configurationNeedsRepair;d["synchronized"]=timeAcquired.load() && time(nullptr)>1700000000;String text;serializeJson(d,text);server.send(200,"application/json",text);});
 server.on("/api/config",HTTP_POST,[]{
  // JSON-only writes block ordinary cross-origin forms, not untrusted clients
  // or DNS rebinding. This endpoint is not authenticated.
  String mediaType=server.header("Content-Type");
  int semicolon=mediaType.indexOf(';'); if(semicolon>=0) mediaType=mediaType.substring(0,semicolon);
  mediaType.trim(); mediaType.toLowerCase();
  if(mediaType!="application/json"){server.send(415,"text/plain","JSON required");return;}
  if(server.arg("plain").length()>2048){server.send(413,"text/plain","Too large");return;}
  if(!server.hasHeader("If-Match")){server.send(428,"text/plain","Reload settings before saving");return;}
  if(server.header("If-Match")!=configRevision()){server.send(412,"text/plain","Settings changed; reload before saving");return;}
  Config candidate;if(!decode(server.arg("plain"),candidate)){server.send(400,"text/plain","Invalid settings: check bounds, colors, brightness, and supported POSIX time zone rule");return;}
  std::unique_ptr<Adafruit_NeoPixel> next;
  if(!strip || candidate.count!=config.count) {
   next=prepareStrip(candidate);
   if(!next){server.send(503,"text/plain","LED buffer allocation failed");return;}
  }
  if(!save(candidate)){server.send(500,"text/plain","Persistence failed");return;}
  if(next) applyStripConfiguration(candidate,std::move(next));
  else { config=candidate; setenv("TZ",config.timezone.c_str(),1); tzset(); }
  configurationNeedsRepair=false;
  bumpRevision();server.sendHeader("ETag",configRevision());server.send(200,"application/json","{\"saved\":true}");
 });
 const char* headers[]={"Content-Type","If-Match"};server.collectHeaders(headers,2);server.begin();
 Serial.println("Provision: WIFI <ssid><TAB><password> then Enter. Button toggles idle light.");previousUs=esp_timer_get_time();
}
void loop() {
 uint64_t now=esp_timer_get_time();oscillator.advance((now-previousUs)/1000000.0,config.period);previousUs=now;
 server.handleClient();
 while(Serial.available()) {
  char ch=Serial.read();if(ch=='\n'){
   if(!serialOverflow && serialLine.startsWith("WIFI ")){int split=serialLine.indexOf('\t',5);if(split>5){String ssid=serialLine.substring(5,split),pass=serialLine.substring(split+1);
    JsonDocument d;d["ssid"]=ssid;d["password"]=pass;String text;serializeJson(d,text);String checkedSsid,checkedPass;
    if(!wifiCredentials(text,checkedSsid,checkedPass)) Serial.println("Invalid Wi-Fi credentials (SSID 1-32 bytes, password empty or 8-63 bytes)");
    else if(prefs.putString("wifi",text)!=text.length()) Serial.println("Wi-Fi credential persistence failed");
    else {WiFi.begin(ssid.c_str(),pass.c_str());Serial.println("Wi-Fi credentials saved");}
   }}
   if(serialOverflow) Serial.println("Serial line too long; discarded");
   serialLine="";serialOverflow=false;
  }else if(ch!='\r' && !serialOverflow){if(serialLine.length()<160)serialLine+=ch;else {serialLine="";serialOverflow=true;}}
 }
 bool pressed=digitalRead(BUTTON_PIN)==LOW;
 if(pressed!=rawButton){rawButton=pressed;buttonChanged=millis();}
 if(millis()-buttonChanged>=40 && stableButton!=rawButton){stableButton=rawButton;if(stableButton && !configurationNeedsRepair){Config next=config;next.idle=!next.idle;if(save(next)){config=next;bumpRevision();}}}
 if(strip && now-lastFrame>=20000){lastFrame=now;timeval wallTime;gettimeofday(&wallTime,nullptr);time_t epoch=wallTime.tv_sec;
  if(!configurationNeedsRepair && timeAcquired.load() && epoch>1700000000){tm local;localtime_r(&epoch,&local);renderEach({local.tm_hour,local.tm_min,local.tm_sec,wallTime.tv_usec/1000000.0},config,oscillator,[](int i,Color pixel){strip->setPixelColor(i,pixel.r,pixel.g,pixel.b);});}
  else { strip->clear(); }
  strip->show();
 }
 static uint32_t reported=0;if(WiFi.status()==WL_CONNECTED && millis()-reported>30000){reported=millis();Serial.println(WiFi.localIP());}
 delay(1);
}
