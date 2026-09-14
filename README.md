# Pocket Nexus

Pocket Nexus is a modular firmware project for the **M5Stack StickS3**. One firmware image provides a portrait launcher containing dashboards, connected-data pages, sensor utilities, IR tools, a screen saver, and mini tools.

## Install without a local development environment

Open the browser installer in **Chrome or Edge**:

**https://ximinhu66.github.io/Pocket_Nexus/**

Connect the StickS3 by USB-C, put it in download mode if needed, select the `USB JTAG/serial debug unit` port, and install. GitHub Actions builds and publishes the latest `main` firmware automatically.

When upgrading, keep device data if prompted so the locally stored Wi-Fi configuration is preserved. On a clean install Pocket Nexus creates a temporary password-protected setup hotspot; the StickS3 screen shows the hotspot name and one-time password. Connect to it and open `192.168.4.1` to save 2.4 GHz Wi-Fi credentials locally on the device.

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

Pocket Nexus also includes a **Screen Saver** app and automatic idle screen saver.

## v0.3

The current hardware-testable build includes:

- 135×240 portrait interface
- Explicit M5GFX fonts and revised spacing to avoid clipped text on the real panel
- Full-screen 16-bit M5Canvas rendering to reduce LCD flicker and improve image color
- Front blue KEY1 as **A** (Next / Action)
- Side KEY2 as **B** (Open / Back)
- More visible battery icon, percentage, battery bar, voltage and charging indication
- Secure temporary Wi-Fi setup portal
- Desk information pager
- Level / attitude display using the built-in BMI270 IMU
- NTP clock
- Countdown foundation
- IMU tilt-ball mini game
- Dice/randomizer with speaker feedback
- Wi-Fi status monitor
- Screen Saver app plus automatic screen saver after 60 seconds idle
- Optional full-screen 135×240 RGB565 custom screen saver image
- Real IR Analyzer using the StickS3 built-in IR receiver and ESP32 legacy RMT driver, including NEC address/command/raw display and raw-signal detection
- UI scaffolds for stocks, calendar, RSS/news and AI voice

## Custom screen saver

Without a custom image, Pocket Nexus uses a minimal clock/date/battery screen saver. To embed an image, see [`docs/SCREENSAVER.md`](docs/SCREENSAVER.md). The intended workflow is to provide a desired image, convert it to 135×240 RGB565 in the repository, let GitHub Actions compile it, then reinstall through the same browser installer—no local IDE is required.

## Security

See [`docs/SECURITY.md`](docs/SECURITY.md). The development build intentionally does **not** burn flash-encryption / secure-boot eFuses. Wi-Fi credentials are not stored in the repository or firmware image, but credentials stored in ESP32 NVS are not cryptographically protected against an attacker with physical flash access in the current development configuration.

During normal home-Wi-Fi operation Pocket Nexus does not expose its Wi-Fi setup HTTP server. The configuration server exists only on a short-lived password-protected AP session.

## Development stack

- M5Stack StickS3
- PlatformIO (cloud build through GitHub Actions)
- Arduino framework
- M5Unified + M5GFX
- M5PM1
- ESP32 legacy RMT API for the current Arduino Core 2 / ESP-IDF 4 build environment

The PlatformIO environment follows M5Stack's StickS3 configuration (`espressif32@6.12.0`, `esp32-s3-devkitc-1`, 8MB partitions, OPI PSRAM).

## Development docs

- [`docs/SETUP.md`](docs/SETUP.md) - local setup fallback and troubleshooting
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) - app architecture and API direction
- [`docs/ROADMAP.md`](docs/ROADMAP.md) - milestones and module status
- [`docs/SECURITY.md`](docs/SECURITY.md) - current threat model and security controls
- [`docs/SCREENSAVER.md`](docs/SCREENSAVER.md) - custom screen saver image workflow
