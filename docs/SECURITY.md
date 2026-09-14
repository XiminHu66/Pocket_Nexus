# Pocket Nexus security notes

## Current v0.2 network exposure

Pocket Nexus does not embed home Wi-Fi credentials in the public GitHub repository or the downloadable firmware image.

During normal operation on the home Wi-Fi network, the current v0.2 firmware does not start an inbound HTTP server. Its current network activity is limited to client-side Wi-Fi connectivity and NTP time synchronization. The setup WebServer is only started while the device is in the dedicated setup hotspot mode.

## Wi-Fi setup hardening

The v0.2 setup flow uses the following controls:

- A fresh random 12-character WPA2 hotspot password is generated every setup session.
- The password is shown only on the StickS3 display.
- The temporary AP allows at most one client.
- The setup portal automatically closes after 10 minutes.
- Setup uses AP-only mode, so the configuration server is not simultaneously exposed on the home LAN.
- The credential POST requires a random per-session setup token to reduce CSRF risk.
- Setup responses are marked no-cache and use frame/content security headers.
- When saved credentials already exist but normal Wi-Fi connection fails, Pocket Nexus does not automatically expose a setup hotspot. The user must explicitly open Wi-Fi Monitor and hold the side B key.

## Credential storage

Credentials are stored locally using ESP32 Preferences/NVS so they survive reboot. In the current development build, flash encryption and NVS encryption are not enabled. Therefore a person with physical possession of the device and the ability to dump flash may be able to recover stored credentials.

ESP-IDF supports NVS encryption tied to platform flash encryption. That is appropriate to evaluate for a production/locked-down build, but enabling flash encryption / secure boot changes flashing and recovery behavior and can involve irreversible eFuse changes. It should not be enabled casually on a development StickS3.

## Firmware distribution

The browser installer is published through GitHub Pages over HTTPS. GitHub Actions compiles the source in `main`, creates the merged ESP32-S3 image, and publishes the installer. The firmware currently has no automatic OTA updater and therefore no always-on remote firmware update service.

## Rules for future connected apps

Future Stock, Calendar, RSS and AI modules should:

- use HTTPS for Internet APIs;
- avoid embedding long-lived private API keys in the public firmware;
- prefer a small authenticated Pocket Nexus backend / Cloudflare Worker that returns only the data the device needs;
- use short-lived or revocable device tokens;
- never expose a general-purpose unauthenticated HTTP server on the home LAN;
- avoid logging credentials, auth headers or private calendar/news payloads to Serial;
- validate response sizes and JSON fields before rendering them.

## Remaining risks

No embedded consumer device can be described as having zero security risk. The main residual risk in the current development build is physical extraction of unencrypted NVS data. Network exposure is intentionally kept small: the normal home-Wi-Fi mode has no inbound application server, and the configuration server is isolated to a short-lived password-protected AP session.
