# QA probe sources for the USB configuration branch

These are review artifacts, not part of the project's test suite. They were written against `arena/106609fc-wall-clock` at `3316caa` and run on x86-64 Linux; results are recorded in [../QA-Review-USB-Config.md](../QA-Review-USB-Config.md). They are provided so the findings can be re-checked and so the client/firmware contract test recommended there can be adopted.

All three expect the reviewed tree somewhere on disk. Set `TREE` to a checkout of that branch before building:

```sh
TREE=/path/to/wall-clock-usb          # contains src/main.cpp, test/host/stubs, tools/serial_config.py
JSON=/path/to/arduinojson-7.2.1/src   # pinned ArduinoJson sources

g++ -std=gnu++17 -O0 -g -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
    -I"$TREE" -I"$TREE/test/host/stubs" -I"$TREE/include" -isystem "$JSON" \
    serial_protocol_probe.cpp "$TREE/test/host/stubs/host_stubs.cpp" -o serial_protocol_probe
./serial_protocol_probe
```

`serial_protocol_probe.cpp` includes the firmware directly and drives its serial stub. It covers 17 cases the branch's harness does not assert: time-zone application reaching libc, blank-line and interleaved-command aborts, ping mid-transfer, chunk past the declared length, non-ASCII chunks, invalid time-zone rejection with state invariance, and the listener staying closed. Expected output: 17 `PASS` lines, `0 failures`.

`serial_pty_bridge.cpp` is the missing seam: it runs the real firmware loop and bridges its serial stub to a real PTY, printing the device path, so the shipped POSIX client can be pointed at the shipped firmware. Build it the same way (replace the source file name), then run `TREE=... BRIDGE=./serial_pty_bridge ./serial_e2e.sh`. Expected: `e2e failures: 0`.

`serial_e2e.sh` requires `tools/serial_config.py` from the reviewed tree and exercises `get`, a chunked `set`, an invalid document, a time-zone change and a missing device path.

Adopting this as a permanent test would close finding F1: today the client is tested only against a hand-written Python fake and the firmware only against a hand-written Node client, so a client-side protocol change can break interoperability without any gate failing.
