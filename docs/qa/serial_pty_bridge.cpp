// Bridge the firmware's Serial stub to a real PTY so the shipped POSIX client
// tool can be exercised end to end against the shipped firmware code.
#include "src/main.cpp"
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
int main() {
  setup();  // open NVS, register routes, print the boot banners
  const int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master < 0) return 2;
  if (grantpt(master) || unlockpt(master)) return 3;
  const char* path = ptsname(master);
  if (!path) return 4;
  if (write(STDOUT_FILENO, path, strlen(path)) < 0) return 5;
  if (write(STDOUT_FILENO, "\n", 1) < 0) return 5;
  fcntl(master, F_SETFL, O_NONBLOCK);
  while (true) {
    char buf[512];
    const ssize_t n = read(master, buf, sizeof buf);
    for (ssize_t i = 0; i < n; ++i) Serial.in.push_back(buf[i]);
    Serial.out.clear();
    loop();
    const char* p = Serial.out.data();
    size_t left = Serial.out.size();
    while (left) {
      const ssize_t w = write(master, p, left);
      if (w <= 0) break;
      p += w; left -= (size_t)w;
    }
    usleep(300);
  }
}
