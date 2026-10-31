#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_timer.h>
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
Adafruit_NeoPixel strip(60,LED_PIN,NEO_GRB+NEO_KHZ800);
uint64_t previousUs=0, lastFrame=0;
uint32_t buttonChanged=0;
bool rawButton=false, stableButton=false;
String serialLine;

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
void apply(const Config& c) {
 strip.clear();strip.show();config=c;strip.updateLength(c.count);
 setenv("TZ",c.timezone.c_str(),1);tzset();
}
void setup() {
 Serial.begin(115200);pinMode(BUTTON_PIN,INPUT_PULLUP);prefs.begin("wallclock",false);
 Config loaded;if(decode(prefs.getString("config",""),loaded))config=loaded;
 strip.begin();apply(config);
 WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
 String ssid=prefs.getString("ssid","");if(ssid.length())WiFi.begin(ssid.c_str(),prefs.getString("password","").c_str());
 configTzTime(config.timezone.c_str(),"pool.ntp.org","time.nist.gov");
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",WEB_UI);});
 server.on("/api/config",HTTP_GET,[]{JsonDocument d;encode(d,config);String text;serializeJson(d,text);server.send(200,"application/json",text);});
 server.on("/api/status",HTTP_GET,[]{JsonDocument d;d["connected"]=WiFi.status()==WL_CONNECTED;d["synchronized"]=time(nullptr)>1700000000;String text;serializeJson(d,text);server.send(200,"application/json",text);});
 server.on("/api/config",HTTP_POST,[]{
  // Browser writes must be same-origin JSON; no CORS is enabled.
  if(server.header("Content-Type")!="application/json"){server.send(415,"text/plain","JSON required");return;}
  if(server.arg("plain").length()>2048){server.send(413,"text/plain","Too large");return;}
  Config candidate;if(!decode(server.arg("plain"),candidate)){server.send(400,"text/plain","Invalid settings: check bounds, colors, and low < high");return;}
  if(!save(candidate)){server.send(500,"text/plain","Persistence failed");return;}
  apply(candidate);server.send(200,"application/json","{\"saved\":true}");
 });
 const char* headers[]={"Content-Type"};server.collectHeaders(headers,1);server.begin();
 Serial.println("Provision: WIFI <ssid><TAB><password> then Enter. Button toggles idle light.");previousUs=esp_timer_get_time();
}
void loop() {
 uint64_t now=esp_timer_get_time();oscillator.advance((now-previousUs)/1000000.0,config.period);previousUs=now;
 server.handleClient();
 while(Serial.available()) {
  char ch=Serial.read();if(ch=='\n'){
   if(serialLine.startsWith("WIFI ")){int split=serialLine.indexOf('\t',5);if(split>5){String ssid=serialLine.substring(5,split),pass=serialLine.substring(split+1);prefs.putString("ssid",ssid);prefs.putString("password",pass);WiFi.begin(ssid.c_str(),pass.c_str());Serial.println("Wi-Fi credentials saved");}}
   serialLine="";
  }else if(ch!='\r'){if(serialLine.length()<160)serialLine+=ch;else serialLine="";}
 }
 bool pressed=digitalRead(BUTTON_PIN)==LOW;
 if(pressed!=rawButton){rawButton=pressed;buttonChanged=millis();}
 if(millis()-buttonChanged>=40 && stableButton!=rawButton){stableButton=rawButton;if(stableButton){Config next=config;next.idle=!next.idle;if(save(next))config=next;}}
 if(now-lastFrame>=20000){lastFrame=now;time_t epoch=time(nullptr);
  if(epoch>1700000000){tm local;localtime_r(&epoch,&local);auto pixels=render({local.tm_hour,local.tm_min,local.tm_sec},config,oscillator);for(int i=0;i<config.count;++i)strip.setPixelColor(i,pixels[i].r,pixels[i].g,pixels[i].b);}
  else { strip.clear(); }
  strip.show();
 }
 static uint32_t reported=0;if(WiFi.status()==WL_CONNECTED && millis()-reported>30000){reported=millis();Serial.println(WiFi.localIP());}
 delay(1);
}
