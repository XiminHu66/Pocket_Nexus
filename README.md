# Pocket Nexus

Pocket Nexus is a modular firmware project for the **M5Stack StickS3**. One firmware image provides a small launcher containing dashboards, connected-data pages, sensor utilities, and mini tools.

## Selected apps

The current plan is based on the following selected ideas:

1. **Pocket Nexus launcher / dashboard**
4. **Stock alert pager**
5. **AI push-to-talk assistant**
8. **Desk information pager**
11. **Calendar next-event view**
12. **RSS / news pager**
13. **IR signal analyzer**
16. **Level / attitude tool**
21. **NTP clock**
22. **Countdown**
23. **IMU tilt game**
24. **Dice / randomizer**
25. **Wi-Fi monitor**

## v0.1

The first hardware-testable version includes:

- Two-button Pocket Nexus launcher
- Wi-Fi + NTP time synchronization
- Desk information pager
- Level / attitude display using the built-in BMI270 IMU
- NTP clock
- Configurable local countdown
- IMU tilt-ball mini game
- Dice/randomizer with speaker feedback
- Wi-Fi status monitor
- UI scaffolds for stocks, calendar, RSS/news, AI voice, and IR analysis

The network-backed apps and full IR RMT decoder are deliberately staged for v0.2 after the basic firmware is confirmed on the physical StickS3.

## Development stack

- M5Stack StickS3
- PlatformIO
- Arduino framework
- M5Unified
- M5PM1

The PlatformIO environment follows M5Stack's StickS3 configuration (`espressif32@6.12.0`, `esp32-s3-devkitc-1`, 8MB partitions, OPI PSRAM).

## Start here

See [`docs/SETUP.md`](docs/SETUP.md) for the complete local build and flashing procedure.

```bash
git clone https://github.com/XiminHu66/Pocket_Nexus.git
cd Pocket_Nexus
git checkout dev/v0.1
```

Then copy `include/secrets.example.h` to `include/secrets.h`, add Wi-Fi credentials, and build/upload with PlatformIO.

## Development docs

- [`docs/SETUP.md`](docs/SETUP.md) - local setup, build, upload, controls, troubleshooting
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) - app architecture and API direction
- [`docs/ROADMAP.md`](docs/ROADMAP.md) - milestones and module status
