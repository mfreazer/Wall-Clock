#include "timezone_rule.h"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

using wallclock::validTimezone;
static void check(bool ok, const std::string& detail) {
  if (!ok) { std::cerr << "FAIL: " << detail << '\n'; std::exit(1); }
}
static void expectLocal(time_t timestamp, int hour, int minute, int second, int dst) {
  std::tm local{};
  check(localtime_r(&timestamp,&local)!=nullptr,"localtime failed");
  check(local.tm_hour==hour && local.tm_min==minute && local.tm_sec==second && local.tm_isdst==dst,
        "Toronto DST transition at " + std::to_string(timestamp));
}
int main() {
  for (const std::string tz : {"UTC0", "EST5EDT,M3.2.0/2,M11.1.0/2", "NST3:30NDT,M3.2.0/0:01,M11.1.0/0:01", "AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", "AAA+24", "BBB-0:45:30"})
    check(validTimezone(tz),"valid rule rejected: " + tz);
  for (const std::string tz : {"", "Not/A/Zone", "America/Toronto", "UTC", "UT0", "ABCDEFGHIJK0", "UTC0garbage", "UTC25", "UTC24:01", "UTC0:60", "UTC0:00:60", "UTC1:2:3:4", "UTC+", "EST5EDT", "EST5EDT,M0.2.0,M11.1.0", "EST5EDT,M13.2.0,M11.1.0", "EST5EDT,M3.0.0,M11.1.0", "EST5EDT,M3.6.0,M11.1.0", "EST5EDT,M3.2.7,M11.1.0", "EST5EDT,M3.2.0/24,M11.1.0", "EST5EDT,M3.2.0/-1,M11.1.0", "EST5EDT,M3.2.0,M11.1.0junk", "<+05>-5", "EST5EDT,J60,J300", "UTC0\n"})
    check(!validTimezone(tz),"invalid/unsupported rule accepted: " + tz);
  check(!validTimezone(std::string("UTC0\0BAD",8)),"embedded NUL accepted");
  check(!validTimezone(std::string(97,'A')),"overlong rule accepted");
  check(setenv("TZ","EST5EDT,M3.2.0/2,M11.1.0/2",1)==0,"setenv failed");
  tzset();
  // Host libc integration only. Target newlib needs independent verification.
  expectLocal(1772953199,1,59,59,0);
  expectLocal(1772953200,3,0,0,1);
  expectLocal(1793512799,1,59,59,1);
  expectLocal(1793512800,1,0,0,0);
  std::cout << "Timezone rule and host DST tests passed\n";
}
