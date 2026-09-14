# Pocket Nexus roadmap

## v0.1 - Hardware foundation

- [x] PlatformIO StickS3 configuration
- [x] Launcher and two-button navigation
- [x] Wi-Fi connection
- [x] NTP time sync
- [x] Desk pager
- [x] Level / attitude tool
- [x] Clock
- [x] Local countdown
- [x] Tilt-ball IMU game
- [x] Dice / randomizer
- [x] Wi-Fi monitor
- [x] Pages/scaffolds for Stocks, Voice AI, Calendar, News, IR Analyzer
- [ ] Verify on the user's physical StickS3

## v0.2 - Live data + IR

- [ ] Split apps/services out of `main.cpp`
- [ ] Device API client with compact JSON models
- [ ] Stock alert endpoint + detail page
- [ ] Calendar next-event endpoint
- [ ] RSS/news endpoint + paging
- [ ] Multiple countdowns from API
- [ ] Full IR RMT receiver
- [ ] NEC decoder + raw pulse view
- [ ] Enter/exit IR app safely disables/restores speaker amplifier
- [ ] Better reconnect/backoff behavior

## v0.3 - Voice

- [ ] Push-to-talk capture using built-in microphone
- [ ] Upload PCM/WAV to backend
- [ ] STT request
- [ ] LLM response
- [ ] TTS playback through built-in speaker
- [ ] Short commands for Pocket Nexus apps

## v0.4 - Product polish

- [ ] Settings page
- [ ] Brightness and timeout controls
- [ ] Battery status
- [ ] Deep sleep / wake strategy
- [ ] Cached last-known network data
- [ ] Notification priority system
- [ ] OTA update path
