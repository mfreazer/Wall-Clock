#include "clock_engine.h"
#include <cstdlib>
#define assert(condition) do { if (!(condition)) { std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " << #condition << "\n"; std::exit(1); } } while (false)
#include <iostream>
using namespace wallclock;
bool same(Color a,Color b) { return a.r==b.r && a.g==b.g && a.b==b.b; }
int main() {
 Config c; assert(valid(c));
 for(int n: {1,7,60,144,300}) for(bool direction:{false,true}) {
  c.count=n;c.clockwise=direction;c.offset=n-1;
  for(int i=0;i<43200;++i) { int p=mapPosition(i/720.0,c);assert(p>=0 && p<n); }
 }
 c=Config{};assert(mapPosition(0,c)==0);assert(mapPosition(59,c)==59);
 assert(hourPosition({1,30,0})==7.5);assert(hourPosition({12,0,0})==0);
 for(double p:{3.,4.5,6.}) {
  c.period=p;Oscillator o;assert(o.hour(c)==c.low);assert(o.minute(c)==c.high);
  o.advance(p/2,p);assert(std::abs(o.hour(c)-c.high)<1e-8);
  o.advance(p/2,p);assert(std::abs(o.hour(c)-c.low)<1e-8);
 }
 c=Config{};Oscillator o;auto a=render({0,0,0},c,o);assert(same(a[0],scale(c.second,c.high)));
 a=render({0,0,1},c,o);assert(same(a[0],add(scale(c.am,c.low),scale(c.minute,c.high))));
 assert(same(a[2],Color{}));c.idle=true;a=render({0,0,1},c,o);assert(same(a[2],scale(c.background,c.low)));
 assert(same(add({250,200,0},{20,100,5}),Color{255,255,5}));
 c.idle=false;auto am=render({0,1,2},c,o),pm=render({12,1,2},c,o);assert(same(am[0],scale(c.am,c.low)));assert(same(pm[0],scale(c.pm,c.low)));
 o.advance(1,c.period);double phase=o.phase();render({23,59,59},c,o);assert(o.phase()==phase);c.period=3;assert(o.phase()==phase);
 c.low=c.high;assert(!valid(c));c=Config{};c.period=NAN;assert(!valid(c));c=Config{};c.count=0;assert(!valid(c));
 std::cout<<"Clock engine tests passed\n";
}
