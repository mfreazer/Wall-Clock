// Host-only Preferences (NVS) shim backed by a std::map.
// Set Preferences::failWrites to simulate a full/failing NVS partition.
#pragma once

#include <Arduino.h>

class PreferencesStore {
 public:
  std::map<std::string, std::string> kv;
};

class Preferences {
 public:
  static bool failWrites;
  static bool failBegin;
  PreferencesStore store;
  bool started = false;

  bool begin(const char*, bool) { started = !failBegin; return started; }
  bool isKey(const char* key) { return started && store.kv.count(key) != 0; }

  String getString(const char* key, const String& defaultValue) {
    if (!started) return defaultValue;
    auto it = store.kv.find(key);
    return it == store.kv.end() ? defaultValue : String(it->second);
  }

  size_t putString(const char* key, const String& value) {
    if (!started || failWrites) return 0;
    store.kv[key] = value.s;
    return value.length();
  }
};
