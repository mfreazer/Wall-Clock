// Host-only esp_timer shim: the tests drive fakeUs to move the clock forward.
#pragma once

#include <cstdint>

extern int64_t fakeUs;
inline int64_t esp_timer_get_time() { return fakeUs; }
