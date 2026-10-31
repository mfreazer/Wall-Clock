ESP32 LED Ring Wall Clock

System Requirements Specification (SyRS)

Document ID: WC-SyRS
Version: 1.0-draft
Status: Requirements baseline for review
System: ESP32-based wall clock with addressable LED ring

1. Purpose

This document specifies the externally observable behavior of a wall clock that uses an ESP32 development board and an addressable LED strip arranged in a ring.

The display represents hours, minutes, and seconds through colored LED indications. Hours and minutes use smooth, phase-shifted brightness modulation. Seconds use constant high brightness and take precedence when indications collide on the same physical LED.

The system supports physical controls and a local web interface.

2. Scope

The system includes:

- An ESP32 development board.
- An addressable LED strip arranged in a ring.
- Wi-Fi connectivity and network time synchronization.
- Hour, minute, and second indications.
- Configurable indication colors and illumination parameters.
- Physical controls.
- A local web interface.
- Persistent configuration.

The following are not yet specified: exact ESP32 board, LED technology, LED count, power supply, physical controls, web-interface details, and timekeeping behavior during network outages or power loss.

3. Terminology

Term| Definition
Physical LED| An individually addressable LED in the installed strip.
Logical position| A position representing a time value independently of the number of physical LEDs.
Indication| The visual contribution associated with hours, minutes, or seconds.
Collision| Two or more indications mapping to the same physical LED.
Modulation period (P)| The duration of one complete brightness cycle.
Low level (B_L)| The configured low brightness level for hour and minute indications.
High level (B_H)| The configured high brightness level, also used by the second indication.
AM/PM| The two 12-hour portions of the day, distinguished by hour-indication color.

4. System requirements

4.1 Timekeeping

WC-SYS-001 — Time source
The system shall obtain time synchronization over Wi-Fi from a network time service.

WC-SYS-002 — Displayed time
The system shall derive hour, minute, and second values from its current timekeeping state.

WC-SYS-003 — Time zone
The system shall use a defined time zone when deriving local time. The configuration mechanism and default time zone are TBD.

WC-SYS-004 — Synchronization failure
The system shall provide a defined indication of time synchronization status. The indication mechanism is TBD.

WC-SYS-005 — Network outage behavior
The system's behavior when Wi-Fi or the time service is unavailable shall be specified before implementation acceptance.

WC-SYS-006 — Restart behavior
The system's timekeeping behavior following a restart or power interruption shall be specified before implementation acceptance.

4.2 LED display and geometry

WC-SYS-010 — Physical layout
The system shall use an addressable LED strip arranged in a circular ring.

WC-SYS-011 — Configurable LED count
The system shall support a configured physical LED count (N). The requirements shall not assume (N=60).

WC-SYS-012 — Logical minute positions
The system shall represent minute values using 60 logical positions, numbered 0 through 59.

WC-SYS-013 — Logical second positions
The system shall represent second values using 60 logical positions, numbered 0 through 59.

WC-SYS-014 — Physical position mapping
The system shall map logical positions to physical LED indices according to a defined mapping rule. The mapping rule for arbitrary (N) is TBD.

WC-SYS-015 — Hour dial
The system shall represent the hour indication on a 12-position dial.

WC-SYS-016 — Continuous hour position
The hour indication's logical position shall advance continuously as minutes and seconds advance, rather than remaining fixed at an hour mark until the next hour.

WC-SYS-017 — Hour position quantization
The system shall convert the continuous hour position into one or more physical LED indices according to the defined physical mapping rule. Quantization and rounding behavior are TBD.

WC-SYS-018 — Ring orientation
The physical reference position for logical position zero and the direction of increasing physical LED indices shall be defined before acceptance testing.

WC-SYS-019 — Bounds safety
The system shall not issue LED writes to physical indices outside the configured range (0) through (N-1).

4.3 Colors

WC-SYS-020 — Distinct time colors
The system shall provide configurable colors for hour, minute, and second indications.

WC-SYS-021 — AM color
The system shall allow configuration of an hour-indication color for AM.

WC-SYS-022 — PM color
The system shall allow configuration of a distinct hour-indication color for PM.

WC-SYS-023 — AM/PM selection
The system shall select the configured hour color according to whether the displayed local time falls in the AM or PM portion of the day.

WC-SYS-024 — Idle illumination
The system shall support an idle state in which LEDs not assigned an active indication are off.

WC-SYS-025 — Idle illumination control
The system shall allow idle illumination to be enabled or disabled independently of time indications.

WC-SYS-026 — Idle color
When idle illumination is enabled, the system shall use a configurable idle color. White shall be the initial default.

4.4 Brightness modulation

WC-SYS-030 — Modulated indications
The hour and minute indications shall use smooth periodic brightness modulation.

WC-SYS-031 — Modulation period
The modulation period (P) shall be configurable from 3 seconds to 6 seconds, inclusive.

WC-SYS-032 — Default period
The default modulation period shall be 4.5 seconds.

WC-SYS-033 — Shared brightness levels
The hour and minute indications shall use the same configurable low and high brightness levels, (B_L) and (B_H).

WC-SYS-034 — Brightness range
The low and high brightness levels shall be configurable as percentages of maximum brightness and shall satisfy:

[
0 \le B_L < B_H \le 100
]

WC-SYS-035 — Smooth waveform
The modulation shall use the following normalized cosine waveform:

[
f(t)=\frac{1-\cos(2\pi t/P)}{2}
]

The hour brightness shall be defined by:

[
B_h(t)=B_L+(B_H-B_L)f(t)
]

The minute brightness shall use the same waveform shifted by half a period:

[
B_m(t)=B_L+(B_H-B_L)f(t+P/2)
]

Here, (t) is elapsed time on the modulation timebase. The resulting modulation shall be continuous at period boundaries.

WC-SYS-036 — Phase relationship
The minute modulation shall be phase-shifted by (P/2) relative to the hour modulation.

WC-SYS-037 — Continuous modulation
The modulation shall run continuously, independently of changes in the displayed hour, minute, or second values.

WC-SYS-038 — Configuration changes
Changes to the period or brightness levels shall take effect without restarting the modulation oscillator or resetting its phase.

WC-SYS-039 — Second brightness
The second indication shall use constant high brightness (B_H), without hour/minute brightness modulation.

4.5 Collision handling

WC-SYS-040 — Second-indication priority
If a second indication and one or more other time indications map to the same physical LED, the system shall display only the second-indication color at high brightness on that LED.

WC-SYS-041 — Hour-minute blending
If hour and minute indications map to the same physical LED and no second indication maps to that LED, the system shall combine their color contributions additively.

WC-SYS-042 — RGB addition
For a shared hour-minute LED, the system shall scale each configured RGB color by its corresponding modulation brightness and add the contributions independently for the red, green, and blue channels.

WC-SYS-043 — Output limiting
The system shall constrain each output color channel to the representable range supported by the selected LED hardware.

WC-SYS-044 — Collision consistency
Collision handling shall follow the same priority and blending rules for all physical LED indices.

4.6 User controls and configuration

WC-SYS-050 — Physical controls
The system shall provide physical controls. Their specific functions and interaction behavior are TBD.

WC-SYS-051 — Web interface
The system shall provide a web interface for user interaction and configuration.

WC-SYS-052 — Configuration functions
The functions exposed through the physical controls and web interface shall be defined before acceptance testing.

WC-SYS-053 — Modulation configuration
The user shall be able to configure the modulation period and shared low/high brightness levels within their specified valid ranges.

WC-SYS-054 — Color configuration
The user shall be able to configure the hour AM color, hour PM color, minute color, second color, and idle color.

WC-SYS-055 — Idle configuration
The user shall be able to enable or disable idle illumination.

WC-SYS-056 — Configuration validation
The system shall reject invalid configuration values without applying them.

WC-SYS-057 — Configuration persistence
Valid configuration settings shall persist across restarts and power interruptions.

WC-SYS-058 — Local access policy
The intended network access scope and any authentication requirements for the web interface are TBD.

4.7 Timing and rendering

WC-SYS-060 — Display refresh
The system shall update the LED output as time values or modulation brightness change.

WC-SYS-061 — Independent timebases
The system shall maintain wall-clock timekeeping and modulation phase as logically independent quantities.

WC-SYS-062 — Refresh continuity
LED refresh operations shall not reset the modulation phase.

WC-SYS-063 — Update latency
The maximum permitted delay between a logical display change and the corresponding physical LED update is TBD.

WC-SYS-064 — Timing accuracy
The permitted timekeeping error under defined operating conditions is TBD.

4.8 Power and operating limits

WC-SYS-070 — Brightness limits
The system shall respect configured brightness limits when generating LED output.

WC-SYS-071 — Electrical safety
The implementation shall operate within the electrical and power limits of the selected ESP32 board, LED strip, and power supply.

WC-SYS-072 — Power supply
The required supply voltage, current capacity, wiring, and grounding arrangement shall be defined after the LED hardware is selected.

5. Verification strategy

Each requirement shall be verified by inspection, analysis, or test as appropriate. The following minimum test cases shall be included.

Test ID| Verification objective
WC-T-001| Verify minute logical positions for values 0 and 59.
WC-T-002| Verify second logical positions for values 0 and 59.
WC-T-003| Verify hour-position progression within an hour and across an hour boundary.
WC-T-004| Verify the defined logical-to-physical mapping at the minimum and maximum valid physical LED indices.
WC-T-005| Verify the modulation waveform at its low and high extrema and at the period boundary.
WC-T-006| Verify modulation periods of 3 seconds, 4.5 seconds, and 6 seconds.
WC-T-007| Verify the half-period phase offset between hour and minute modulation.
WC-T-008| Verify that changes in displayed time do not reset modulation phase.
WC-T-009| Verify that second-indication priority suppresses hour and minute contributions on a colliding LED.
WC-T-010| Verify additive RGB blending of colliding hour and minute indications.
WC-T-011| Verify output-channel limiting at the supported maximum.
WC-T-012| Verify AM/PM color selection at the AM/PM boundary.
WC-T-013| Verify idle LEDs are off by default and use the configured idle color when enabled.
WC-T-014| Verify valid configuration persistence and invalid configuration rejection.
WC-T-015| Verify operation when Wi-Fi synchronization is unavailable according to the approved timekeeping policy.
WC-T-016| Verify physical controls and web-interface functions against the approved interaction specification.

6. Outstanding decisions

The following items remain open and must not be treated as finalized design requirements:

1. Mapping of 60 logical minute/second positions to an arbitrary physical LED count (N).
2. Physical orientation of LED index zero and direction of increasing indices.
3. Exact quantization and rounding of the continuous hour position.
4. Timekeeping behavior during Wi-Fi loss, time-service failure, cold start, and power interruption.
5. Default and configurable time zone.
6. Physical control functions and web-interface functions.
7. Web-interface network access and authentication policy.
8. Configuration defaults beyond the 4.5-second modulation period and black idle state.
9. LED update latency and timekeeping accuracy limits.
10. LED hardware, power supply, and electrical limits.

Baseline rule: a requirement marked TBD is not considered ready for final acceptance verification until its missing value or behavior has been agreed and recorded.
