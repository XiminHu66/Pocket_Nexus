# Pocket Nexus

Pocket Nexus is a modular firmware project for the **M5Stack StickS3**. One firmware image provides a portrait launcher containing dashboards, connected-data pages, sensor utilities, and mini tools.

## Install without a local development environment

Open the browser installer in **Chrome or Edge**:

**https://ximinhu66.github.io/Pocket_Nexus/**

Connect the StickS3 by USB-C, put it in download mode if needed, select the `USB JTAG/serial debug unit` port, and install. GitHub Actions builds and publishes the latest `main` firmware automatically.

On first boot Pocket Nexus creates a temporary password-protected setup hotspot. The StickS3 screen shows the hotspot name and one-time password; connect to it and open `192.168.4.1` to save the 2.4 GHz Wi-Fi credentials locally on the device.

## Selected apps

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

## v0.2

The current hardware-testable build includes:

- 135×240 portrait interface
- Larger card-based menu and status bar
- Full-screen M5Canvas rendering to reduce LCD flicker
- Front blue KEY1 as **A** (Next / Action)
- Side KEY2 as **B** (Open / Back)
- Battery + Wi-Fi status
- Secure temporary Wi-Fi setup portal
- Desk information pager
- Level / attitude display using the built-in BMI270 IMU
- NTP clock
- Countdown foundation
- IMU tilt-ball mini game
- Dice/randomizer with speaker feedback
- Wi-Fi status monitor
- UI scaffolds for stocks, calendar, RSS/news, AI voice, and IR analysis

The network-backed apps and full IR RMT decoder remain staged for the next milestones.

## Security

See [`docs/SECURITY.md`](docs/SECURITY.md). The development build intentionally does **not** burn flash-encryption / secure-boot eFuses. Wi-Fi credentials are not stored in the repository or firmware image, but credentials stored in ESP32 NVS are not cryptographically protected against an attacker with physical flash access in the current development configuration.

## Development stack

- M5Stack StickS3
- PlatformIO (cloud build through GitHub Actions)
- Arduino framework
- M5Unified + M5GFX
- M5PM1

The PlatformIO environment follows M5Stack's StickS3 configuration (`espressif32@6.12.0`, `esp32-s3-devkitc-1`, 8MB partitions, OPI PSRAM).

## Development docs

- [`docs/SETUP.md`](docs/SETUP.md) - local setup fallback and troubleshooting
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) - app architecture and API direction
- [`docs/ROADMAP.md`](docs/ROADMAP.md) - milestones and module status
- [`docs/SECURITY.md`](docs/SECURITY.md) - current threat model and security controls
