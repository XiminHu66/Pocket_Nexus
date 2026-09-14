# Pocket Nexus architecture

## Goal

One StickS3 firmware, many small apps. Pocket Nexus should behave like a tiny launcher rather than requiring a separate firmware image for each utility.

## v0.1 app map

| User selection | Module | v0.1 state |
|---|---|---|
| 1 | Pocket Nexus shell | Working |
| 4 | Stock alert pager | UI/API scaffold |
| 5 | AI push-to-talk | UI/audio scaffold |
| 8 | Desk information pager | Working |
| 11 | Calendar next event | UI/API scaffold |
| 12 | RSS/news pager | UI/API scaffold |
| 13 | IR signal analyzer | RMT integration scaffold |
| 16 | Level/attitude | Working |
| 21 | NTP clock | Working |
| 22 | Countdown | Working with configured epoch |
| 23 | IMU tilt game | Working |
| 24 | Dice/randomizer | Working |
| 25 | Wi-Fi monitor | Working |

## Layers

```text
+-----------------------------+
|       Pocket Nexus UI       |
| launcher / pages / controls |
+-----------------------------+
| App modules                 |
| stocks calendar news ...    |
+-----------------------------+
| Services                    |
| Wi-Fi / time / API / audio  |
+-----------------------------+
| M5Unified + ESP32 Arduino   |
+-----------------------------+
| M5Stack StickS3 hardware    |
+-----------------------------+
```

v0.1 intentionally keeps implementation in a single `src/main.cpp` while hardware APIs and navigation are being validated on a real StickS3. Once the first hardware test passes, v0.2 will split apps/services into separate source files.

## Network API direction

Pocket Nexus should not perform expensive aggregation on-device. Network-backed apps should request small device-oriented payloads from one API base URL.

Suggested future endpoints:

```text
GET  /v1/device/summary
GET  /v1/stocks/alerts
GET  /v1/calendar/next
GET  /v1/news/top
GET  /v1/countdowns
POST /v1/ai/voice
```

This keeps API keys and heavy processing off the StickS3 and allows Pocket Nexus to reuse existing Personal Tools services.

## IR Analyzer constraint

StickS3 IR receive should use the ESP32 RMT peripheral. The speaker amplifier must be disabled while IR reception is active. The IR module is therefore isolated as a dedicated app state in the next milestone so entering/exiting the page can safely switch audio hardware state.
