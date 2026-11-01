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
  PreferencesStore store;

  bool begin(const char*, bool) { return true; }

  String getString(const char* key, const String& defaultValue) {
    auto it = store.kv.find(key);
    return it == store.kv.end() ? defaultValue : String(it->second);
  }

  size_t putString(const char* key, const String& value) {
    if (failWrites) return 0;
    store.kv[key] = value.s;
    return value.length();
  }
};
