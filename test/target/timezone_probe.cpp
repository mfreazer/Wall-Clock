// ESP32/newlib integration probe, executed by the sandbox emulator.
// No network, NVS or physical hardware is exercised.
#include <Arduino.h>
#include <cstdlib>
#include <ctime>
#include <string>
#include "timezone_rule.h"

struct Case {
  const char* rule;
  time_t timestamp;
  int year, month, day, hour, minute, second, dst;
};

// UTC instants and expected local civil times, calculated independently.
static const Case cases[] = {
  {"UTC0", 1772953200, 2026, 3, 8, 7, 0, 0, 0},
  {"EST5EDT,M3.2.0/2,M11.1.0/2", 1772953199, 2026, 3, 8, 1, 59, 59, 0},
  {"EST5EDT,M3.2.0/2,M11.1.0/2", 1772953200, 2026, 3, 8, 3, 0, 0, 1},
  {"EST5EDT,M3.2.0/2,M11.1.0/2", 1793512799, 2026, 11, 1, 1, 59, 59, 1},
  {"EST5EDT,M3.2.0/2,M11.1.0/2", 1793512800, 2026, 11, 1, 1, 0, 0, 0},
  {"AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", 1775318399, 2026, 4, 5, 2, 59, 59, 1},
  {"AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", 1775318400, 2026, 4, 5, 2, 0, 0, 0},
  {"AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", 1791043199, 2026, 10, 4, 1, 59, 59, 0},
  {"AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", 1791043200, 2026, 10, 4, 3, 0, 0, 1},
  {"NST3:30NDT,M3.2.0/2,M11.1.0/2", 1772947799, 2026, 3, 8, 1, 59, 59, 0},
  {"NST3:30NDT,M3.2.0/2,M11.1.0/2", 1772947800, 2026, 3, 8, 3, 0, 0, 1},
  {"NST3:30NDT,M3.2.0/2,M11.1.0/2", 1793507399, 2026, 11, 1, 1, 59, 59, 1},
  {"NST3:30NDT,M3.2.0/2,M11.1.0/2", 1793507400, 2026, 11, 1, 1, 0, 0, 0},
  {"EST5EDT,M3.2.0/0:01:30,M11.1.0/23:59:59", 1772946089, 2026, 3, 8, 0, 1, 29, 0},
  {"EST5EDT,M3.2.0/0:01:30,M11.1.0/23:59:59", 1772946090, 2026, 3, 8, 1, 1, 30, 1},
  {"IST-5:30", 1767225600, 2026, 1, 1, 5, 30, 0, 0},
  {"ABCDEFGHIJ-1", 1767225600, 2026, 1, 1, 1, 0, 0, 0},
  {"AAA+24", 1772953200, 2026, 3, 7, 7, 0, 0, 0},
  {"BBB-0:45:30", 1772953200, 2026, 3, 8, 7, 45, 30, 0},
};

void setup() {
  Serial.begin(115200);
  int failures=0;
  for (const auto& c : cases) {
    std::tm local{};
    bool ok=wallclock::validTimezone(c.rule) && setenv("TZ",c.rule,1)==0;
    if (ok) { tzset(); ok=localtime_r(&c.timestamp,&local)!=nullptr; }
    if (ok) ok=local.tm_year+1900==c.year && local.tm_mon+1==c.month && local.tm_mday==c.day &&
               local.tm_hour==c.hour && local.tm_min==c.minute && local.tm_sec==c.second && local.tm_isdst==c.dst;
    if (!ok) {
      ++failures;
      Serial.printf("TZ_CASE_FAIL %s %lld got %04d-%02d-%02d %02d:%02d:%02d dst=%d\n", c.rule,
                    static_cast<long long>(c.timestamp),local.tm_year+1900,local.tm_mon+1,local.tm_mday,
                    local.tm_hour,local.tm_min,local.tm_sec,local.tm_isdst);
    }
  }
  if (failures) Serial.printf("TZ_TARGET_FAIL %d/%u\n",failures,static_cast<unsigned>(sizeof(cases)/sizeof(cases[0])));
  else Serial.printf("TZ_TARGET_PASS %u\n",static_cast<unsigned>(sizeof(cases)/sizeof(cases[0])));
}
void loop() { delay(1000); }
