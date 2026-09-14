#pragma once

// Copy this file to include/secrets.h and edit the values.
// include/secrets.h is ignored by Git.

#define PN_WIFI_SSID "YOUR_WIFI_NAME"
#define PN_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// POSIX timezone string for Pacific Time, including daylight saving time.
#define PN_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"

// Reserved for network-backed Pocket Nexus modules.
#define PN_API_BASE_URL ""

// Optional local countdown. Use a Unix timestamp in seconds; 0 disables it.
#define PN_COUNTDOWN_LABEL "Next Event"
#define PN_COUNTDOWN_EPOCH 0LL
