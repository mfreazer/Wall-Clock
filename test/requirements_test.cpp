// Requirement-linked native checks. These do not establish hardware acceptance.
// Unlike engine_test assertions, these checks remain active with or without NDEBUG.
#include "clock_engine.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace wallclock;
void check(bool condition, const char* detail) { if (!condition) throw std::runtime_error(detail); }
bool equal(Color a, Color b) { return a.r==b.r && a.g==b.g && a.b==b.b; }
bool near(double a,double b) { return std::abs(a-b)<1e-8; }
void positions() {
 Config c; Oscillator o;
 for (int value : {0,59}) {
  auto minute=render({6,value,30},c,o);
  check(equal(minute[value],scale(c.minute,c.high)),"minute endpoint");
  auto second=render({6,20,value},c,o);
  check(equal(second[value],scale(c.second,c.high)),"second endpoint");
 }
 check(near(hourPosition({1,0,0}),5),"hour mark");
 check(near(hourPosition({1,30,0}),7.5),"half-hour");
 check(hourPosition({1,59,59})<hourPosition({2,0,0}),"hour boundary progression");
 check(near(hourPosition({11,59,59}),60-1.0/720),"dial wrap predecessor");
 check(near(hourPosition({12,0,0}),0),"dial wrap");
 for(int n : {1,7,60,144,300}) for(bool clockwise : {true,false}) {
  c.count=n;c.offset=n-1;c.clockwise=clockwise;
  check(mapPosition(0,c)==c.offset,"reference offset");
  for(int s=0;s<43200;++s) {
   int index=mapPosition(s/720.0,c);
   check(index>=0 && index<n,"mapping bounds");
  }
  check(render({23,59,59},c,o).size()==static_cast<size_t>(n),"frame bounds");
 }
 c=Config{};check(mapPosition(0.5,c)==1,"provisional tie rounding");
}
void modulation() {
 Config c;
 check(near(c.period,4.5),"default period");
 for(double period : {3.,4.5,6.}) {
  c.period=period;Oscillator o;
  for(int step=0;step<=100;++step) {
   double expected=c.low+(c.high-c.low)*(1-std::cos(2*3.14159265358979323846*step/100))/2;
   check(near(o.hour(c),expected),"cosine samples");
   check(near(o.hour(c)+o.minute(c),c.low+c.high),"half-period relationship");
   auto frame=render({3,20,40},c,o);
   check(equal(frame[40],scale(c.second,c.high)),"constant second brightness");
   o.advance(period/100,period);
  }
 }
 Oscillator o;o.advance(1,c.period);double phase=o.phase();
 render({1,2,3},c,o);render({23,59,59},c,o);
 check(near(phase,o.phase()),"wall time/render does not reset phase");
 c.period=3;c.low=5;c.high=80;
 check(near(phase,o.phase()),"config mutation does not reset core phase");
 o.advance(0.3,c.period);check(near(o.phase(),std::fmod(phase+0.1,1.0)),"new period phase rate");
}
void collisions() {
 Config c;c.low=40;c.high=100;c.am={255,255,255};c.pm={1,2,3};c.minute={255,255,255};
 Oscillator o;
 for(int offset=0;offset<60;++offset) {
  c.offset=offset;
  auto all=render({0,0,0},c,o);
  check(equal(all[offset],scale(c.second,c.high)),"triple collision priority");
  auto hm=render({0,0,1},c,o);
  check(equal(hm[offset],Color{255,255,255}),"additive channel saturation");
  auto hs=render({0,1,0},c,o);
  check(equal(hs[offset],scale(c.second,c.high)),"hour-second priority");
  auto ms=render({6,1,1},c,o);
  check(equal(ms[mapPosition(1,c)],scale(c.second,c.high)),"minute-second priority");
 }
 c=Config{};
 auto am=render({11,59,58},c,o),pm=render({12,0,1},c,o);
 check(equal(am[0],scale(c.am,c.low)),"AM before noon");
 check(equal(pm[0],add(scale(c.pm,c.low),scale(c.minute,c.high))),"PM at noon");
 check(equal(render({0,1,2},c,o)[0],scale(c.am,c.low)),"AM at midnight");
 check(equal(render({6,10,20},c,o)[1],Color{}),"idle off default");
 check(equal(c.background,Color{255,255,255}),"idle default white");
 c.idle=true;c.background={100,50,20};
 check(equal(render({6,10,20},c,o)[1],scale(c.background,c.low)),"configured idle");
}
void validation() {
 Config c;check(valid(c),"valid defaults");
 for(double period : {3.,4.5,6.}) { c.period=period;check(valid(c),"period boundary accepted"); }
 for(double period : {2.99,6.01,double(NAN),double(INFINITY)}) { c.period=period;check(!valid(c),"invalid period rejected"); }
 c=Config{};c.low=0;c.high=100;check(valid(c),"brightness endpoints");
 for(auto levels : {std::pair<double,double>{-1,50},{50,50},{60,50},{0,101},{NAN,50},{0,INFINITY}}) {
  c.low=levels.first;c.high=levels.second;check(!valid(c),"invalid brightness rejected");
 }
 for(int count : {0,301,-1}) { c=Config{};c.count=count;check(!valid(c),"invalid count"); }
 c=Config{};c.offset=60;check(!valid(c),"invalid offset");
 c=Config{};c.timezone="";check(!valid(c),"empty zone");
}
int main() {
 int failed=0;
 for(auto test : {std::pair<const char*,void(*)()>{"positions [WC-T-001..004]",positions},
   {"modulation [WC-T-005..008 + core WC-SYS-038/039/062]",modulation},
   {"collisions/colors/idle [WC-T-009..013]",collisions},
   {"validation only [partial WC-T-014]",validation}}) {
  try { test.second();std::cout<<"PASS "<<test.first<<'\n'; }
  catch(const std::exception& e) { ++failed;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n'; }
 }
 return failed ? 1 : 0;
}
