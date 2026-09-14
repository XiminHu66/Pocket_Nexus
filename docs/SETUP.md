# Pocket Nexus local setup

## 1. Install the tools

1. Install **Visual Studio Code**.
2. Open VS Code -> Extensions.
3. Search for **PlatformIO IDE** and install it.
4. Restart VS Code if requested.

You do not need to create a separate PlatformIO project; this repository already contains `platformio.ini`.

## 2. Clone Pocket Nexus

Open PowerShell (Windows) or Terminal (macOS/Linux):

```bash
git clone https://github.com/XiminHu66/Pocket_Nexus.git
cd Pocket_Nexus
git checkout dev/v0.1
```

Then open the repository folder in VS Code.

## 3. Configure Wi-Fi

Copy:

```text
include/secrets.example.h
```

to:

```text
include/secrets.h
```

Edit only `secrets.h`:

```cpp
#define PN_WIFI_SSID "your Wi-Fi name"
#define PN_WIFI_PASSWORD "your Wi-Fi password"
```

`include/secrets.h` is ignored by Git, so credentials are not committed.

The default timezone is Pacific Time and automatically handles DST:

```cpp
#define PN_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"
```

## 4. Optional countdown

Set `PN_COUNTDOWN_LABEL` and `PN_COUNTDOWN_EPOCH` in `secrets.h`.

`PN_COUNTDOWN_EPOCH` is the target Unix timestamp in seconds. Leave it at `0` to disable the countdown.

## 5. Connect the StickS3

Use a USB-C cable capable of data transfer.

If the board is not detected for flashing:

1. Hold the side reset button for about 2 seconds.
2. Release it when the internal green LED blinks.
3. The StickS3 should now be in download mode.

## 6. Build

From the VS Code PlatformIO sidebar:

```text
PROJECT TASKS
  m5stack-sticks3
    General
      Build
```

Or from a terminal with PlatformIO CLI installed:

```bash
pio run
```

The first build downloads the ESP32 platform and libraries and can take longer than later builds.

## 7. Upload

PlatformIO sidebar:

```text
PROJECT TASKS
  m5stack-sticks3
    General
      Upload
```

Or:

```bash
pio run -t upload
```

## 8. Serial monitor

Optional, but useful when debugging:

```bash
pio device monitor
```

The project uses 115200 baud.

## 9. Controls in v0.1

At the main menu:

- **Button A**: move to next app
- **Button B**: open selected app

Inside an app:

- **Button A**: refresh or perform the app action
- **Button B**: return to the main menu

Examples:

- Dice: A rolls again.
- Wi-Fi Monitor: A attempts reconnect.
- Level: updates automatically.
- Tilt Game: tilt the StickS3 to move the ball.

## 10. What should work without Wi-Fi

- Launcher
- Level / attitude
- Tilt game
- Dice / randomizer
- IR Analyzer page scaffold

With Wi-Fi configured:

- NTP clock
- Desk pager time/date
- Wi-Fi monitor
- Countdown if its epoch is configured

Stock Alerts, AI Push-to-Talk, Calendar, RSS/News, and full IR decoding are staged for the next milestone because they require endpoint/protocol integration rather than only local UI.

## Troubleshooting

### Upload port not found

Enter StickS3 download mode using the reset-button procedure above, then retry Upload.

### Clock shows `--:--`

Check Wi-Fi credentials first. NTP synchronization can take a few seconds after startup.

### Wi-Fi will not connect

StickS3 supports 2.4 GHz Wi-Fi. Make sure the SSID is available on 2.4 GHz and credentials are correct.

### Buttons do not respond

The firmware calls `M5.update()` continuously. If you edit the main loop later, keep that call; M5Unified uses it to refresh button state.
