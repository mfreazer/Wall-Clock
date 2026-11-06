// Host-only bridge: connect the real firmware's Serial stub to a POSIX PTY.
// Adapted from the independent QA probe in review commit 7d85d2d; the client
// is a separate process using the shipped serial_config.py.
#include "../../src/main.cpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

static bool writeAll(int fd, const char* data, size_t remaining) {
  while (remaining) {
    const ssize_t n = write(fd, data, remaining);
    if (n > 0) { data += n; remaining -= static_cast<size_t>(n); continue; }
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd pfd = {fd, POLLOUT, 0};
      const int ready = poll(&pfd, 1, 5000);
      if (ready < 0 && errno == EINTR) continue;
      if (ready > 0 && (pfd.revents & POLLOUT)) continue;
    }
    return false;
  }
  return true;
}

int main() {
  setup();
  const int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) return 2;
  const char* path = ptsname(master);
  if (!path) return 2;
  // Keep a slave open between client invocations so the master does not see
  // EIO. The client opens the same slave and sets it to raw mode itself.
  const int slave = open(path, O_RDWR | O_NOCTTY);
  if (slave < 0) return 2;
  std::printf("%s\n", path);
  std::fflush(stdout);
  Serial.out.clear(); // Boot output is not an answer to a client request.
  for (;;) {
    struct pollfd pfd = {master, POLLIN, 0};
    const int ready = poll(&pfd, 1, 5);
    if (ready < 0) { if (errno == EINTR) continue; return 3; }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return 3;
    if (pfd.revents & POLLIN) {
      char buffer[512];
      const ssize_t n = read(master, buffer, sizeof buffer);
      if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return 3;
      for (ssize_t i = 0; i < n; ++i) Serial.in.push_back(buffer[i]);
    }
    loop();
    if (!Serial.out.empty()) {
      if (!writeAll(master, Serial.out.data(), Serial.out.size())) return 4;
      Serial.out.clear();
    }
  }
}
