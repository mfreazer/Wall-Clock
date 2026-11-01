#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "timezone_rule.h"

namespace wallclock {
struct Color { uint8_t r=0, g=0, b=0; };
struct Config {
  int count=60, offset=0;
  bool clockwise=true, idle=false;
  double period=4.5, low=10, high=50;
  Color am{255,100,20}, pm{180,40,255}, minute{20,150,255}, second{255,30,40}, background{255,255,255};
  std::string timezone="UTC0";
};
inline bool valid(const Config& c) {
  return c.count>=1 && c.count<=300 && c.offset>=0 && c.offset<c.count &&
    std::isfinite(c.period) && c.period>=3 && c.period<=6 &&
    std::isfinite(c.low) && std::isfinite(c.high) && c.low>=0 && c.low<c.high && c.high<=100 &&
    validTimezone(c.timezone);
}
struct Time { int hour, minute, second; double fraction=0; };
inline double hourPosition(Time t) { return (t.hour%12)*5.0+t.minute/12.0+(t.second+t.fraction)/720.0; }
inline int mapPosition(double position, const Config& c) {
  int index=static_cast<int>(std::floor(position*c.count/60.0+0.5))%c.count;
  return ((c.offset+(c.clockwise?index:-index))%c.count+c.count)%c.count;
}
// Phase is integrated from monotonic elapsed time, never derived from wall time.
class Oscillator {
  double phase_=0;
public:
  void advance(double seconds, double period) { phase_=std::fmod(phase_+seconds/period,1.0); }
  double phase() const { return phase_; }
  double hour(const Config& c) const { return c.low+(c.high-c.low)*(1-std::cos(2*3.14159265358979323846*phase_))/2; }
  double minute(const Config& c) const { return c.low+c.high-hour(c); }
};
inline Color scale(Color c, double brightness) {
  return {static_cast<uint8_t>(std::lround(c.r*brightness/100)),static_cast<uint8_t>(std::lround(c.g*brightness/100)),static_cast<uint8_t>(std::lround(c.b*brightness/100))};
}
inline Color add(Color a, Color b) {
  return {static_cast<uint8_t>(std::min(255,int(a.r)+b.r)),static_cast<uint8_t>(std::min(255,int(a.g)+b.g)),static_cast<uint8_t>(std::min(255,int(a.b)+b.b))};
}
// Stream pixels to the output without allocating a frame on every refresh.
template <typename Sink>
inline void renderEach(Time t, const Config& c, const Oscillator& o, Sink sink) {
  int h=mapPosition(hourPosition(t),c), m=mapPosition(t.minute,c), s=mapPosition(t.second,c);
  Color hour=scale(t.hour<12?c.am:c.pm,o.hour(c));
  Color minute=scale(c.minute,o.minute(c));
  Color second=scale(c.second,c.high);
  Color idle=c.idle?scale(c.background,c.low):Color{};
  for (int i=0; i<c.count; ++i) {
    Color pixel=idle;
    if (i==h) pixel=hour;
    if (i==m) pixel=i==h?add(pixel,minute):minute;
    if (i==s) pixel=second;
    sink(i,pixel);
  }
}
inline std::vector<Color> render(Time t, const Config& c, const Oscillator& o) {
  std::vector<Color> pixels(c.count);
  renderEach(t,c,o,[&pixels](int index, Color pixel) { pixels[index]=pixel; });
  return pixels;
}
}
