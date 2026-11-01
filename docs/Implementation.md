# Implementation profile (provisional)

This profile resolves draft TBDs for development, not as approved hardware acceptance criteria. The SyRS remains the normative draft; these choices require owner review before acceptance.

## Geometry and rendering

- Supported count: 1–300 RGB pixels, default 60. Nearest physical pixel: `floor(position * N / 60 + 0.5) mod N`. Ties round toward increasing logical position. The hour uses `(hour % 12)*5 + minute/12 + second/720`.
- Default index 0 is at twelve o'clock and wiring proceeds clockwise. Offset and direction are configurable.
- Default AM is orange, PM purple, minute blue, second red. Low/high defaults are 10%/50%. Idle white is scaled by low brightness, and disabled initially.
- Rendering targets 50 Hz. This is a target, not a verified latency guarantee; HTTP and LED transmission may delay frames.
- Monotonic microsecond time advances an integrated oscillator. Changing period preserves phase; wall-clock corrections do not affect phase. Brightness changes apply immediately.
- RGB output uses rounded 8-bit channels and saturating addition. Seconds replace all other contributions on their pixel.

## Time and connectivity

UTC (`UTC0`) is the initial zone. The zone field accepts a POSIX TZ rule, **not** an IANA zone name. For Eastern Canadian DST use `EST5EDT,M3.2.0/2,M11.1.0/2`. Validation does not check POSIX syntax: malformed values, including IANA names such as `Not/A/Zone`, can be accepted and silently yield UTC. Verify the chosen rule independently; this is an unresolved correctness risk.

ESP32 SNTP synchronizes from pool.ntp.org and time.nist.gov. Before an SNTP synchronization callback has occurred and the epoch is plausible, the ring stays dark. After synchronization, the system clock free-runs through outages and Wi-Fi reconnects automatically. A cold restart requires synchronization again; no battery RTC or saved epoch is used. Status is visible through `/api/status` and the web page. Status means an SNTP callback has occurred since boot and the epoch is plausible; it does not report when the last sync occurred or guarantee the clock is currently accurate. Holdover accuracy and NTP failure diagnostics remain unqualified.

## Configuration and controls

The active-low button between GPIO27 and ground toggles idle illumination on a debounced press (40 ms), persisting only on state changes. No boot/strapping pin is used. The web interface exposes count, orientation, time zone, all five colors, brightness and period. It loads and saves complete JSON documents using `/api/config`. GET returns an ETag; POST requires the exact `If-Match` ETag and returns 428 if missing or 412 if stale. Successful POST returns the new ETag. Clients must reload after a conflict. Button changes also advance the revision, so a stale page cannot overwrite them. A new random revision epoch is generated on boot; it is a concurrency token, not an authentication mechanism. Invalid or incomplete documents are rejected before saving or applying. When the LED count changes, the replacement LED buffer is allocated before saving and applying a web update; allocation or NVS failure returns an error without changing the active configuration. The previous driver remains in use on failure. Other settings updates do not blank or reallocate the strip. Rendering streams pixels directly to the driver without allocating a frame on each refresh. Corrupt persisted configuration falls back to defaults at boot. The oscillator is not reset on updates.

The interface is HTTP without authentication, intended **only for an isolated, trusted local Wi-Fi network**. Do not forward port 80, put it on a guest/shared network, or treat it as production-secure. No CORS is enabled; JSON-only writes prevent ordinary cross-origin browser forms. This does not protect against untrusted LAN clients. DNS rebinding is not prevented. The 2048-byte application check occurs after WebServer buffers the request, so it is not a memory-exhaustion defense. Configuration writes and button toggles have no rate limit and repeatedly write flash. TLS/authentication, early request limits and write-rate/endurance policy remain future work.

Wi-Fi credentials are provisioned over USB serial and held together in one ESP32 NVS value, not source control. A legacy two-key credential pair is read if the new value is absent; it is not erased automatically. Serial lines longer than 160 bytes are discarded in full. NVS is not encrypted by this example. The UI does not expose Wi-Fi credentials.

## Hardware profile and safety

Development target: ESP32 DevKit (`esp32dev`), WS2812B-compatible 5 V RGB strip in GRB order, GPIO18 data, GPIO27 button. Override pins with compiler definitions if necessary. RGBW strips are not supported by this driver profile.

Use an appropriately rated external regulated 5 V supply, common ground, a suitable 3.3 V to 5 V data level shifter, a 330–470 ohm series data resistor and a bulk capacitor at the strip input. Do not power the ring through the ESP32 board. Size wiring, fuses and power injection for the installed strip; a conservative RGB estimate is 60 mA per pixel at full white (300 pixels can require 18 A). Brightness settings are **not** an electrical current limiter: additive collisions can saturate channels. Disconnect power before wiring. The specific strip, supply and installation require electrical verification.

## Verification status

`test/engine_test.cpp` and `test/requirements_test.cpp` check arbitrary-count bounds, direction, mapping endpoints, fractional hour movement, waveform extrema and phase offset at all three specified periods, collision precedence, additive clipping, AM/PM selection, idle output, basic validation and phase independence. Run with CMake/CTest or the compiler command in README.

Not yet verified: ESP32 compilation, NVS persistence and interrupted writes, network outage/reconnection, button behavior, web request integration, electrical safety, actual refresh latency, time accuracy and perceived modulation. Browser interactions and JSON persistence need integration tests. Do not interpret passing native tests as hardware acceptance.
