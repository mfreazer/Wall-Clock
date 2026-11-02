#pragma once

#include <cstddef>
#include <string>

namespace wallclock {
// Deliberately supported subset of POSIX TZ: alphabetic 3-10 character names,
// an explicit standard UTC offset, and optionally a DST name/offset with two
// explicit Mmonth.week.weekday transitions. Each transition may have /hh[:mm[:ss]].
// This avoids newlib's silent fallback and implementation-dependent DST defaults.
// It is not an IANA zone name parser or a complete POSIX TZ implementation.
class TimezoneRuleParser {
  const std::string& text_;
  std::size_t pos_ = 0;

  static bool digit(char c) { return c >= '0' && c <= '9'; }
  static bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
  bool take(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; }
    return false;
  }
  bool number(int& value, int maxDigits) {
    value=0;
    int length=0;
    while (pos_ < text_.size() && digit(text_[pos_]) && length < maxDigits) {
      value=value*10+(text_[pos_++]-'0');
      ++length;
    }
    return length > 0 && (pos_ == text_.size() || !digit(text_[pos_]));
  }
  bool name() {
    const std::size_t start=pos_;
    while (pos_ < text_.size() && letter(text_[pos_])) ++pos_;
    const std::size_t length=pos_-start;
    return length >= 3 && length <= 10;
  }
  bool clockValue(int maxHours, bool allowSign) {
    if (allowSign && !take('-')) take('+');
    int hour=0, minute=0, second=0;
    if (!number(hour,2) || hour>maxHours) return false;
    if (take(':')) {
      if (!number(minute,2) || minute>59) return false;
      if (take(':') && (!number(second,2) || second>59)) return false;
    }
    return hour != maxHours || maxHours != 24 || (minute == 0 && second == 0);
  }
  bool transition() {
    int month=0, week=0, day=0;
    if (!take('M') || !number(month,2) || month<1 || month>12 || !take('.') ||
        !number(week,1) || week<1 || week>5 || !take('.') ||
        !number(day,1) || day>6) return false;
    return !take('/') || clockValue(23,false);
  }
 public:
  explicit TimezoneRuleParser(const std::string& text) : text_(text) {}
  bool parse() {
    if (text_.empty() || text_.size()>96 || !name() || !clockValue(24,true)) return false;
    if (pos_==text_.size()) return true;
    if (!name()) return false;
    if (pos_<text_.size() && (digit(text_[pos_]) || text_[pos_]=='+' || text_[pos_]=='-'))
      if (!clockValue(24,true)) return false;
    return take(',') && transition() && take(',') && transition() && pos_==text_.size();
  }
};
inline bool validTimezone(const std::string& text) { return TimezoneRuleParser(text).parse(); }
}
