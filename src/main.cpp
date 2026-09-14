#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>
#include "driver/rmt.h"
#include "freertos/ringbuf.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define PN_WIFI_SSID ""
#define PN_WIFI_PASSWORD ""
#define PN_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"
#define PN_API_BASE_URL ""
#define PN_COUNTDOWN_LABEL "Next Event"
#define PN_COUNTDOWN_EPOCH 0LL
#endif

#if __has_include("screensaver_image.h")
#include "screensaver_image.h"
#define PN_HAS_CUSTOM_SCREENSAVER 1
#else
#define PN_HAS_CUSTOM_SCREENSAVER 0
#endif

namespace pn {

constexpr const char* VERSION = "0.4";
constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t FG = TFT_WHITE;
constexpr uint16_t MUTED = 0x7BEF;
constexpr uint16_t ACCENT = TFT_CYAN;
constexpr uint16_t OK = TFT_GREEN;
constexpr uint16_t WARN = TFT_YELLOW;
constexpr uint16_t PANEL = 0x0018;
constexpr uint16_t PANEL2 = 0x0210;
constexpr uint16_t BLUE = 0x041F;
constexpr uint32_t SETUP_TIMEOUT_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t SCREENSAVER_TIMEOUT_MS = 60UL * 1000UL;
constexpr int IR_RECEIVE_PIN = 42;

struct AppEntry { const char* menuName; const char* title; };

enum class AppId : uint8_t {
  Dashboard, Stocks, VoiceAI, DeskPager, Calendar, News, IRAnalyzer,
  Level, Clock, Countdown, TiltGame, Dice, WiFiMonitor, ScreenSaver, Count
};

constexpr AppEntry APPS[] = {
  {"NEXUS", "Pocket Nexus"}, {"STOCKS", "Stock Alerts"}, {"VOICE AI", "Voice AI"},
  {"PAGER", "Desk Pager"}, {"CALENDAR", "Calendar"}, {"NEWS", "RSS / News"},
  {"IR", "IR Analyzer"}, {"LEVEL", "Level"}, {"CLOCK", "NTP Clock"},
  {"COUNTDOWN", "Countdown"}, {"GAME", "Tilt Game"}, {"DICE", "Dice"},
  {"WI-FI", "Wi-Fi"}, {"SAVER", "Screen Saver"}
};
constexpr uint8_t APP_COUNT = static_cast<uint8_t>(AppId::Count);

M5Canvas frame(&M5.Display);
uint8_t selected = 0;
bool inApp = false;
AppId current = AppId::Dashboard;
uint8_t pageIndex = 0;
uint32_t lastRender = 0;
uint32_t lastSecond = 0;
uint32_t lastInteractionAt = 0;
bool screensaverActive = false;
int diceValue = 1;
float gameX = 67.0f, gameY = 132.0f, gameVx = 0.0f, gameVy = 0.0f;

WebServer setupServer(80);
bool setupRoutesInstalled = false;
bool setupPortalActive = false;
uint32_t setupStartedAt = 0;
String setupApName, setupApPassword, setupToken;
String configuredSsid, configuredPassword;

RingbufHandle_t irRingBuffer = nullptr;
constexpr rmt_channel_t IR_RMT_CHANNEL = RMT_CHANNEL_0;
bool irActive = false, irHasFrame = false, irFrameValid = false, irRepeatFrame = false;
size_t irSymbolCount = 0;
uint32_t irRawData = 0;
uint16_t irAddress = 0;
uint8_t irCommand = 0;

int W() { return frame.width(); }
int H() { return frame.height(); }

void fontSmall() { frame.setTextFont(&fonts::Font0); frame.setTextSize(1); }
void fontUI() { frame.setTextFont(&fonts::FreeMonoBold9pt7b); frame.setTextSize(1); }
void fontBig() { frame.setTextFont(&fonts::FreeMonoBold9pt7b); frame.setTextSize(2); }

void beginFrame() {
  frame.fillSprite(BG);
  frame.setTextWrap(false);
  frame.setTextColor(FG, BG);
  frame.setTextDatum(top_left);
  fontSmall();
}
void present() { frame.pushSprite(0, 0); }

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool hasWiFiConfig() { return configuredSsid.length() > 0; }

String clipText(const String& value, size_t maxChars) {
  if (value.length() <= maxChars) return value;
  if (maxChars <= 3) return value.substring(0, maxChars);
  return value.substring(0, maxChars - 3) + "...";
}

void drawWrappedText(const String& text, int x, int y, int maxWidth, int lineHeight,
                     int maxLines, uint16_t color = FG, uint16_t bg = BG) {
  fontSmall();
  frame.setTextColor(color, bg);
  String line, word;
  int lines = 0;
  auto flushLine = [&]() {
    if (line.length() && lines < maxLines) {
      frame.setCursor(x, y + lines * lineHeight);
      frame.print(line);
      ++lines;
      line = "";
    }
  };

  for (size_t i = 0; i <= text.length() && lines < maxLines; ++i) {
    char c = (i < text.length()) ? text[i] : ' ';
    if (c == '\n') {
      if (word.length()) { if (line.length()) line += ' '; line += word; word = ""; }
      flushLine();
      continue;
    }
    if (c == ' ' || i == text.length()) {
      if (!word.length()) continue;
      String candidate = line.length() ? line + " " + word : word;
      if (frame.textWidth(candidate) <= maxWidth) {
        line = candidate;
      } else {
        flushLine();
        if (lines >= maxLines) break;
        if (frame.textWidth(word) <= maxWidth) {
          line = word;
        } else {
          String chunk;
          for (size_t j = 0; j < word.length() && lines < maxLines; ++j) {
            String tryChunk = chunk + word[j];
            if (frame.textWidth(tryChunk) > maxWidth && chunk.length()) {
              line = chunk;
              flushLine();
              chunk = String(word[j]);
            } else chunk = tryChunk;
          }
          line = chunk;
        }
      }
      word = "";
    } else word += c;
  }
  if (lines < maxLines) flushLine();
}

void drawBatteryIcon(int x, int y, int level, bool charging) {
  const int w = 24, h = 12;
  frame.drawRoundRect(x, y, w, h, 2, FG);
  frame.fillRect(x + w, y + 4, 2, 4, FG);
  int pct = constrain(level, 0, 100);
  int fillW = (w - 4) * pct / 100;
  uint16_t color = pct <= 15 ? TFT_RED : (pct <= 30 ? WARN : OK);
  if (fillW > 0) frame.fillRoundRect(x + 2, y + 2, fillW, h - 4, 1, color);
  if (charging) {
    fontSmall(); frame.setTextColor(TFT_BLACK, color); frame.setCursor(x + 9, y + 2); frame.print("+");
  }
}

void drawStatusBar(const char* label = "NEXUS") {
  frame.fillRect(0, 0, W(), 28, BLUE);
  fontSmall(); frame.setTextColor(FG, BLUE); frame.setCursor(5, 9); frame.print(label);
  frame.fillCircle(75, 14, 4, wifiConnected() ? OK : (setupPortalActive ? WARN : MUTED));
  const int battery = M5.Power.getBatteryLevel();
  const bool charging = static_cast<bool>(M5.Power.isCharging());
  drawBatteryIcon(84, 8, battery < 0 ? 0 : battery, charging);
  frame.setTextColor(FG, BLUE); frame.setCursor(112, 9);
  if (battery >= 0) frame.printf("%d", battery); else frame.print("--");
}

void drawAppHeader(const char* title) {
  drawStatusBar();
  fontUI(); frame.setTextColor(ACCENT, BG);
  String t(title);
  if (frame.textWidth(t) <= W() - 14) frame.drawString(t, 7, 35);
  else { fontSmall(); frame.setTextColor(ACCENT, BG); frame.setCursor(7, 40); frame.print(t); }
  frame.drawFastHLine(7, 58, W() - 14, MUTED);
}

void drawFooter(const char* a = "A ACTION", const char* b = "B BACK") {
  const int y = H() - 27;
  frame.fillRoundRect(5, y + 2, W() - 10, 23, 6, BLUE);
  fontSmall(); frame.setTextColor(FG, BLUE); frame.setCursor(10, y + 10); frame.print(a);
  int bx = W() - 7 - static_cast<int>(strlen(b)) * 6;
  frame.setCursor(max(70, bx), y + 10); frame.print(b);
}

void drawPageTag(uint8_t page, uint8_t count) {
  if (count <= 1) return;
  fontSmall(); frame.setTextColor(MUTED, BG);
  String p = String(page + 1) + "/" + String(count);
  frame.setCursor(W() - 7 - p.length() * 6, 48); frame.print(p);
}

void drawInfoCard(int y, const char* label, const String& value, uint16_t valueColor = FG) {
  frame.fillRoundRect(7, y, W() - 14, 44, 8, PANEL);
  fontSmall(); frame.setTextColor(MUTED, PANEL); frame.setCursor(13, y + 7); frame.print(label);
  fontUI(); frame.setTextColor(valueColor, PANEL);
  if (frame.textWidth(value) <= W() - 28) frame.drawString(value, 13, y + 20);
  else { fontSmall(); frame.setTextColor(valueColor, PANEL); drawWrappedText(value, 13, y + 20, W() - 28, 12, 2, valueColor, PANEL); }
}

void drawBatteryCard(int y) {
  const int battery = M5.Power.getBatteryLevel();
  const int voltage = M5.Power.getBatteryVoltage();
  const bool charging = static_cast<bool>(M5.Power.isCharging());
  const int pct = battery < 0 ? 0 : constrain(battery, 0, 100);
  frame.fillRoundRect(7, y, W() - 14, 37, 8, PANEL);
  fontSmall(); frame.setTextColor(MUTED, PANEL); frame.setCursor(13, y + 6);
  frame.print(charging ? "BATTERY +CHG" : "BATTERY");
  if (voltage > 0) { frame.setCursor(87, y + 6); frame.printf("%.1fV", voltage / 1000.0f); }
  fontUI(); frame.setTextColor(pct <= 20 ? WARN : FG, PANEL);
  frame.drawString(battery >= 0 ? String(battery) + "%" : "--", 13, y + 14);
  const int barX = 58, barY = y + 20, barW = 60;
  frame.drawRoundRect(barX, barY, barW, 9, 2, MUTED);
  int fillW = (barW - 4) * pct / 100;
  uint16_t color = pct <= 15 ? TFT_RED : (pct <= 30 ? WARN : OK);
  if (fillW > 0) frame.fillRoundRect(barX + 2, barY + 2, fillW, 5, 1, color);
}

bool readTime(struct tm& t) { return getLocalTime(&t, 10); }
String hhmm(bool seconds = false) {
  struct tm t{}; if (!readTime(t)) return seconds ? "--:--:--" : "--:--";
  char buf[16]; strftime(buf, sizeof(buf), seconds ? "%H:%M:%S" : "%H:%M", &t); return String(buf);
}
String dateLine() {
  struct tm t{}; if (!readTime(t)) return "Time not synced";
  char buf[32]; strftime(buf, sizeof(buf), "%a  %b %d", &t); return String(buf);
}
String randomString(size_t length, const char* alphabet) {
  String out; out.reserve(length); size_t n = strlen(alphabet);
  for (size_t i = 0; i < length; ++i) out += alphabet[esp_random() % n];
  return out;
}

void loadWiFiCredentials() {
  if (strlen(PN_WIFI_SSID) > 0) { configuredSsid = PN_WIFI_SSID; configuredPassword = PN_WIFI_PASSWORD; return; }
  Preferences prefs;
  if (prefs.begin("pocket-nexus", true)) {
    configuredSsid = prefs.getString("ssid", ""); configuredPassword = prefs.getString("pass", ""); prefs.end();
  }
}
void saveWiFiCredentials(const String& ssid, const String& password) {
  Preferences prefs;
  if (prefs.begin("pocket-nexus", false)) { prefs.putString("ssid", ssid); prefs.putString("pass", password); prefs.end(); }
}

void addSetupSecurityHeaders() {
  setupServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  setupServer.sendHeader("Pragma", "no-cache");
  setupServer.sendHeader("X-Content-Type-Options", "nosniff");
  setupServer.sendHeader("X-Frame-Options", "DENY");
  setupServer.sendHeader("Referrer-Policy", "no-referrer");
  setupServer.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
}
String setupPageHtml() {
  String p;
  p.reserve(1800);
  p += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Pocket Nexus Setup</title><style>body{font-family:system-ui;background:#0b0d10;color:#fff;padding:24px}main{max-width:520px;margin:auto;background:#151920;padding:24px;border-radius:18px}input,button{box-sizing:border-box;width:100%;font-size:17px;padding:13px;margin:8px 0;border-radius:10px}input{background:#0f1318;color:white;border:1px solid #39434f}button{border:0;background:#35d0ba;color:#07110f;font-weight:700}p{color:#aeb8c5;line-height:1.5}</style></head><body><main><h2>Pocket Nexus Wi-Fi</h2><p>Temporary local setup only. Credentials stay on this device.</p><form method='post' action='/save'><input type='hidden' name='token' value='");
  p += setupToken;
  p += F("'><input name='ssid' maxlength='32' placeholder='2.4 GHz Wi-Fi SSID' required><input name='password' type='password' maxlength='63' placeholder='Wi-Fi password'><button type='submit'>Save & Restart</button></form><p>Hotspot closes automatically after 10 minutes.</p></main></body></html>");
  return p;
}
void renderSetupPortal() {
  beginFrame(); drawStatusBar("SETUP");
  fontUI(); frame.setTextColor(WARN, BG); frame.drawString("Wi-Fi Setup", 7, 36);
  fontSmall(); frame.setTextColor(MUTED, BG); frame.setCursor(8, 66); frame.print("HOTSPOT");
  frame.setTextColor(FG, BG); drawWrappedText(setupApName, 8, 80, W() - 16, 13, 2);
  frame.setTextColor(MUTED, BG); frame.setCursor(8, 111); frame.print("PASSWORD");
  frame.setTextColor(ACCENT, BG); drawWrappedText(setupApPassword, 8, 125, W() - 16, 13, 2, ACCENT);
  frame.setTextColor(MUTED, BG); frame.setCursor(8, 160); frame.print("OPEN 192.168.4.1");
  frame.setCursor(8, 181); frame.print("1 client / 10 min");
  drawFooter("A -", "B CANCEL"); present();
}
void installSetupRoutes() {
  if (setupRoutesInstalled) return;
  setupRoutesInstalled = true;
  setupServer.on("/", HTTP_GET, [](){ addSetupSecurityHeaders(); setupServer.send(200, "text/html; charset=utf-8", setupPageHtml()); });
  setupServer.on("/save", HTTP_POST, [](){
    addSetupSecurityHeaders();
    String token = setupServer.arg("token"), ssid = setupServer.arg("ssid"), pass = setupServer.arg("password");
    if (token != setupToken) { setupServer.send(403, "text/plain", "Invalid token"); return; }
    if (!ssid.length() || ssid.length() > 32 || pass.length() > 63) { setupServer.send(400, "text/plain", "Invalid Wi-Fi credentials"); return; }
    saveWiFiCredentials(ssid, pass);
    setupServer.send(200, "text/html; charset=utf-8", "<html><body><h2>Saved</h2><p>Restarting...</p></body></html>");
    delay(700); ESP.restart();
  });
  setupServer.onNotFound([](){ addSetupSecurityHeaders(); setupServer.sendHeader("Location", "/", true); setupServer.send(302, "text/plain", ""); });
}
void stopSetupPortal() {
  if (!setupPortalActive) return;
  setupServer.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  setupPortalActive = false; setupApPassword = ""; setupToken = "";
}
void startSetupPortal() {
  if (setupPortalActive) return;
  static const char* safe = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  static const char* hex = "0123456789abcdef";
  char suffix[5]; snprintf(suffix, sizeof(suffix), "%04X", static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFF));
  setupApName = String("PN-Setup-") + suffix; setupApPassword = randomString(12, safe); setupToken = randomString(24, hex); setupStartedAt = millis();
  WiFi.disconnect(true, false); delay(50); WiFi.mode(WIFI_AP); WiFi.softAP(setupApName.c_str(), setupApPassword.c_str(), 1, false, 1);
  setupPortalActive = true; installSetupRoutes(); setupServer.begin(); renderSetupPortal();
}
void renderConnecting() {
  beginFrame(); drawAppHeader("Connecting");
  fontUI(); frame.setTextColor(ACCENT, BG); drawWrappedText(configuredSsid, 8, 78, W() - 16, 18, 3, ACCENT);
  fontSmall(); frame.setTextColor(MUTED, BG); frame.setCursor(8, 145); frame.print("Trying saved Wi-Fi..."); present();
}
bool tryWiFiConnection() {
  if (!hasWiFiConfig()) return false;
  stopSetupPortal(); WiFi.mode(WIFI_STA); WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str()); renderConnecting();
  uint32_t started = millis();
  while (!wifiConnected() && millis() - started < 8000) { M5.update(); delay(50); }
  if (!wifiConnected()) return false;
  configTzTime(PN_TIMEZONE, "pool.ntp.org", "time.nist.gov"); return true;
}
void connectWiFi() {
  loadWiFiCredentials();
  if (!hasWiFiConfig()) { startSetupPortal(); return; }
  tryWiFiConnection();
}

bool isPagedApp(AppId app) {
  return app == AppId::Stocks || app == AppId::VoiceAI || app == AppId::Calendar || app == AppId::News ||
         app == AppId::IRAnalyzer || app == AppId::WiFiMonitor || app == AppId::ScreenSaver;
}
uint8_t pageCount(AppId app) { return isPagedApp(app) ? 2 : 1; }

void renderMenu() {
  beginFrame(); drawStatusBar();
  fontSmall(); frame.setTextColor(MUTED, BG); frame.setCursor(7, 34); frame.printf("APPS %02u/%02u", selected + 1, APP_COUNT);
  constexpr uint8_t rows = 4; uint8_t start = selected >= rows ? selected - rows + 1 : 0;
  for (uint8_t row = 0; row < rows; ++row) {
    uint8_t idx = start + row; if (idx >= APP_COUNT) break;
    int y = 48 + row * 39; bool active = idx == selected;
    frame.fillRoundRect(5, y, W() - 10, 34, 7, active ? BLUE : PANEL2);
    if (!active) frame.drawRoundRect(5, y, W() - 10, 34, 7, MUTED);
    fontSmall(); frame.setTextColor(active ? FG : MUTED, active ? BLUE : PANEL2); frame.setCursor(10, y + 13); frame.printf("%02u", idx + 1);
    fontUI(); frame.setTextColor(FG, active ? BLUE : PANEL2); frame.drawString(APPS[idx].menuName, 34, y + 7);
  }
  drawFooter("A NEXT", "B OPEN"); present();
}

void renderDashboard() {
  beginFrame(); drawAppHeader("Pocket Nexus");
  fontBig(); frame.setTextColor(ACCENT, BG); frame.drawString(hhmm(), 8, 65);
  fontSmall(); frame.setTextColor(MUTED, BG); drawWrappedText(dateLine(), 9, 108, W() - 18, 13, 2, MUTED);
  drawInfoCard(126, "WI-FI", wifiConnected() ? String(WiFi.RSSI()) + " dBm" : "Offline", wifiConnected() ? OK : WARN);
  drawBatteryCard(174); drawFooter("A REFRESH", "B BACK"); present();
}

void renderPlaceholder(const char* title, const char* line1, const char* line2) {
  beginFrame(); drawAppHeader(title); drawPageTag(pageIndex, 2);
  if (pageIndex == 0) {
    frame.fillRoundRect(7, 70, W() - 14, 122, 9, PANEL);
    fontUI(); frame.setTextColor(ACCENT, PANEL); drawWrappedText(String(line1), 13, 84, W() - 26, 17, 4, ACCENT, PANEL);
    fontSmall(); frame.setTextColor(FG, PANEL); drawWrappedText(String(line2), 13, 126, W() - 26, 13, 4, FG, PANEL);
  } else {
    frame.fillRoundRect(7, 70, W() - 14, 122, 9, PANEL);
    fontSmall(); frame.setTextColor(FG, PANEL);
    String status = strlen(PN_API_BASE_URL) ? "Backend URL configured. Live endpoint wiring is the next milestone." : "Backend is not configured yet. This page will use a small authenticated Pocket Nexus API rather than storing private API keys on the StickS3.";
    drawWrappedText(status, 13, 84, W() - 26, 14, 8, FG, PANEL);
  }
  fontSmall(); frame.setTextColor(MUTED, BG); frame.setCursor(8, 199); frame.print("hold A: refresh/action");
  drawFooter("A NEXT", "B BACK"); present();
}

void renderDeskPager() {
  beginFrame(); drawAppHeader("Desk Pager");
  fontBig(); frame.setTextColor(ACCENT, BG); frame.drawString(hhmm(), 8, 67);
  fontSmall(); frame.setTextColor(MUTED, BG); drawWrappedText(dateLine(), 9, 111, W() - 18, 13, 2, MUTED);
  drawInfoCard(132, "NETWORK", wifiConnected() ? "Online" : "Offline", wifiConnected() ? OK : WARN);
  drawBatteryCard(174); drawFooter("A REFRESH", "B BACK"); present();
}

void readAccel(float& ax, float& ay, float& az) {
  ax = ay = 0.0f; az = 1.0f;
  if (M5.Imu.update()) { auto d = M5.Imu.getImuData(); ax = d.accel.x; ay = d.accel.y; az = d.accel.z; }
}
void renderLevel() {
  float ax, ay, az; readAccel(ax, ay, az);
  float roll = atan2f(ay, az) * 180.0f / PI;
  float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI;
  beginFrame(); drawAppHeader("Level");
  fontSmall(); frame.setTextColor(FG, BG); frame.setCursor(8, 70); frame.printf("Pitch %5.1f", pitch); frame.setCursor(8, 86); frame.printf("Roll  %5.1f", roll);
  int cx = W()/2, cy = 151; frame.drawCircle(cx, cy, 42, MUTED); frame.drawCircle(cx, cy, 21, MUTED);
  frame.drawFastHLine(cx - 38, cy, 76, MUTED); frame.drawFastVLine(cx, cy - 38, 76, MUTED);
  int bx = constrain(cx + static_cast<int>(ay * 37.0f), cx - 37, cx + 37);
  int by = constrain(cy - static_cast<int>(ax * 37.0f), cy - 37, cy + 37);
  frame.fillCircle(bx, by, 7, ACCENT); drawFooter("A ZERO", "B BACK"); present();
}
void renderClock() {
  beginFrame(); drawAppHeader("NTP Clock");
  fontBig(); frame.setTextColor(ACCENT, BG); frame.drawString(hhmm(), 8, 78);
  fontSmall(); frame.setTextColor(FG, BG); drawWrappedText(dateLine(), 8, 125, W() - 16, 14, 2, FG);
  frame.setTextColor(wifiConnected() ? OK : WARN, BG); frame.setCursor(8, 158); frame.print(wifiConnected() ? "NTP synced" : "Offline / unsynced");
  drawBatteryCard(174); drawFooter("A REFRESH", "B BACK"); present();
}
void renderCountdown() {
  beginFrame(); drawAppHeader("Countdown");
  fontSmall(); frame.setTextColor(ACCENT, BG); drawWrappedText(String(PN_COUNTDOWN_LABEL), 8, 72, W() - 16, 14, 3, ACCENT);
  if (PN_COUNTDOWN_EPOCH <= 0) {
    fontUI(); frame.setTextColor(WARN, BG); frame.drawString("Not set", 8, 122);
    fontSmall(); frame.setTextColor(MUTED, BG); drawWrappedText("Remote countdown sync is planned for the connected backend.", 8, 155, W() - 16, 13, 4, MUTED);
  } else {
    long long delta = static_cast<long long>(PN_COUNTDOWN_EPOCH) - static_cast<long long>(time(nullptr)); bool overdue = delta < 0; if (overdue) delta = -delta;
    long long days = delta / 86400LL; int hours = (delta % 86400LL) / 3600LL;
    fontBig(); frame.setTextColor(FG, BG); frame.drawString(String(days) + "d", 8, 118);
    fontUI(); frame.drawString(String(hours) + "h " + (overdue ? "late" : "left"), 8, 162);
  }
  drawFooter("A REFRESH", "B BACK"); present();
}
void renderDice(bool rollNow) {
  if (rollNow) { diceValue = 1 + static_cast<int>(esp_random() % 6); M5.Speaker.tone(2200, 40); }
  beginFrame(); drawAppHeader("Dice"); frame.fillRoundRect(16, 78, W() - 32, 108, 16, PANEL);
  fontBig(); frame.setTextSize(3); frame.setTextColor(ACCENT, PANEL); frame.setTextDatum(middle_center); frame.drawString(String(diceValue), W()/2, 132); frame.setTextDatum(top_left);
  drawFooter("A ROLL", "B BACK"); present();
}

void renderWiFiMonitor() {
  beginFrame(); drawAppHeader("Wi-Fi"); drawPageTag(pageIndex, 2);
  if (pageIndex == 0) {
    drawInfoCard(68, "STATUS", wifiConnected() ? "Connected" : (hasWiFiConfig() ? "Disconnected" : "Not set"), wifiConnected() ? OK : WARN);
    drawInfoCard(116, "SSID", wifiConnected() ? WiFi.SSID() : (hasWiFiConfig() ? configuredSsid : "None"));
    drawInfoCard(164, "SIGNAL", wifiConnected() ? String(WiFi.RSSI()) + " dBm" : "--");
  } else {
    frame.fillRoundRect(7, 68, W() - 14, 123, 8, PANEL);
    fontSmall(); frame.setTextColor(FG, PANEL);
    String body = wifiConnected()
      ? String("IP: ") + WiFi.localIP().toString() + "\nChannel: " + String(WiFi.channel()) + "\nRSSI: " + String(WiFi.RSSI()) + " dBm\n\nHold A to reconnect. Hold B for secure setup."
      : String("Saved Wi-Fi: ") + (hasWiFiConfig() ? configuredSsid : "none") + "\n\nHold A to reconnect. Hold B to open the password-protected setup hotspot.";
    drawWrappedText(body, 13, 82, W() - 26, 14, 9, FG, PANEL);
  }
  frame.setTextColor(MUTED, BG); frame.setCursor(8, 199); frame.print("hold A: reconnect");
  drawFooter("A NEXT", "B BACK"); present();
}

void renderTiltGame() {
  float ax, ay, az; readAccel(ax, ay, az);
  gameVx = constrain(gameVx + ay * 0.28f, -3.5f, 3.5f); gameVy = constrain(gameVy - ax * 0.28f, -3.5f, 3.5f);
  gameVx *= 0.96f; gameVy *= 0.96f; gameX = constrain(gameX + gameVx, 14.0f, static_cast<float>(W()-14)); gameY = constrain(gameY + gameVy, 76.0f, static_cast<float>(H()-45));
  beginFrame(); drawAppHeader("Tilt Game"); frame.drawRoundRect(7, 66, W()-14, H()-109, 8, MUTED); frame.fillCircle(static_cast<int>(gameX), static_cast<int>(gameY), 7, ACCENT);
  drawFooter("A RESET", "B BACK"); present();
}

bool decodeNEC(const rmt_item32_t* symbols, size_t count, uint32_t* outRaw, bool* outRepeat) {
  *outRaw = 0; *outRepeat = false; if (count < 2) return false;
  uint32_t h0 = symbols[0].duration0, h1 = symbols[0].duration1;
  if (h0 > 8000 && h1 > 4000) { if (count < 33) return false; }
  else if (h0 > 8000 && h1 > 2000 && h1 < 3000) { *outRepeat = true; return false; }
  else return false;
  for (int i=0;i<32;++i) { uint32_t mark=symbols[i+1].duration0, space=symbols[i+1].duration1; if (mark<300||mark>800) return false; if (space>1000) *outRaw |= (1UL<<i); }
  uint8_t cmd=(*outRaw>>16)&0xFF, inv=(*outRaw>>24)&0xFF; return (cmd ^ inv)==0xFF;
}
bool startIrAnalyzer() {
  if (irActive) return true;
  M5.Speaker.end(); M5.Power.setExtOutput(true, m5::ext_none);
  rmt_config_t cfg={}; cfg.rmt_mode=RMT_MODE_RX; cfg.channel=IR_RMT_CHANNEL; cfg.gpio_num=static_cast<gpio_num_t>(IR_RECEIVE_PIN); cfg.clk_div=80; cfg.mem_block_num=2;
  cfg.rx_config.filter_en=true; cfg.rx_config.filter_ticks_thresh=80; cfg.rx_config.idle_threshold=15000;
  if (rmt_config(&cfg)!=ESP_OK || rmt_driver_install(IR_RMT_CHANNEL,2048,0)!=ESP_OK || rmt_get_ringbuf_handle(IR_RMT_CHANNEL,&irRingBuffer)!=ESP_OK || !irRingBuffer || rmt_rx_start(IR_RMT_CHANNEL,true)!=ESP_OK) {
    rmt_driver_uninstall(IR_RMT_CHANNEL); irRingBuffer=nullptr; M5.Power.setExtOutput(false,m5::ext_none); M5.Speaker.begin(); return false;
  }
  irHasFrame=irFrameValid=irRepeatFrame=false; irSymbolCount=0; irActive=true; return true;
}
void stopIrAnalyzer() {
  if (!irActive) return; rmt_rx_stop(IR_RMT_CHANNEL); rmt_driver_uninstall(IR_RMT_CHANNEL); irRingBuffer=nullptr; M5.Power.setExtOutput(false,m5::ext_none); M5.Speaker.begin(); irActive=false;
}
bool pollIrFrame() {
  if (!irActive || !irRingBuffer) return false;
  size_t rxSize=0; auto* items=(rmt_item32_t*)xRingbufferReceive(irRingBuffer,&rxSize,0); if (!items) return false;
  size_t count=rxSize/sizeof(rmt_item32_t); uint32_t raw=0; bool repeat=false; bool valid=decodeNEC(items,count,&raw,&repeat);
  irHasFrame=true; irFrameValid=valid; irRepeatFrame=repeat; irSymbolCount=count; irRawData=raw; irAddress=raw&0xFFFF; irCommand=(raw>>16)&0xFF;
  vRingbufferReturnItem(irRingBuffer,(void*)items); return true;
}
void renderIRAnalyzer() {
  beginFrame(); drawAppHeader("IR Analyzer"); drawPageTag(pageIndex,2);
  if (!irActive) {
    fontSmall(); frame.setTextColor(WARN,BG); drawWrappedText("Receiver unavailable. IR hardware test can be done later.",8,76,W()-16,14,6,WARN);
  } else if (pageIndex == 0) {
    if (!irHasFrame) {
      fontUI(); frame.setTextColor(ACCENT,BG); drawWrappedText("Point remote at StickS3",8,76,W()-16,17,4,ACCENT);
      fontSmall(); frame.setTextColor(FG,BG); drawWrappedText("Press any remote key. Keep the remote at least about 30 cm away.",8,132,W()-16,14,5,FG);
    } else if (irFrameValid) {
      drawInfoCard(72,"PROTOCOL","NEC",OK); drawInfoCard(120,"ADDRESS",String("0x")+String(irAddress,HEX)); drawInfoCard(168,"COMMAND",String("0x")+String(irCommand,HEX));
    } else {
      fontUI(); frame.setTextColor(WARN,BG); frame.drawString(irRepeatFrame ? "NEC repeat" : "Raw signal",8,78);
      fontSmall(); frame.setTextColor(FG,BG); drawWrappedText(String("Captured ")+String(irSymbolCount)+" RMT symbols. Open page 2 for raw details.",8,118,W()-16,14,6,FG);
    }
  } else {
    frame.fillRoundRect(7,70,W()-14,120,8,PANEL); fontSmall(); frame.setTextColor(FG,PANEL);
    String body;
    if (!irHasFrame) body="Waiting for a signal. The speaker amplifier is disabled while IR receive is active.";
    else if (irFrameValid) body=String("Protocol: NEC\nAddress: 0x")+String(irAddress,HEX)+"\nCommand: 0x"+String(irCommand,HEX)+"\nRaw: 0x"+String(irRawData,HEX)+"\nSymbols: "+String(irSymbolCount);
    else body=String("Signal captured but NEC validation failed.\nSymbols: ")+String(irSymbolCount)+"\n\nThis is useful for non-NEC remotes; raw timing support will be expanded later.";
    drawWrappedText(body,13,82,W()-26,14,9,FG,PANEL);
  }
  frame.setTextColor(MUTED,BG); frame.setCursor(8,199); frame.print("hold A: clear"); drawFooter("A NEXT","B BACK"); present();
}

void renderDefaultScreensaver() {
  beginFrame(); fontSmall(); frame.setTextColor(MUTED,BG); frame.setCursor(8,10); frame.print("POCKET NEXUS");
  fontBig(); frame.setTextColor(FG,BG); frame.setTextDatum(middle_center); frame.drawString(hhmm(),W()/2,92);
  fontSmall(); frame.setTextColor(ACCENT,BG); frame.drawString(dateLine(),W()/2,132);
  int bat=M5.Power.getBatteryLevel(); drawBatteryIcon((W()-24)/2,166,bat<0?0:bat,static_cast<bool>(M5.Power.isCharging()));
  frame.setTextColor(MUTED,BG); frame.drawString("press any key",W()/2,207); frame.setTextDatum(top_left); present();
}
void renderCustomScreensaver() {
#if PN_HAS_CUSTOM_SCREENSAVER && defined(PN_SCREENSAVER_JPEG)
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.drawJpg(PN_SCREENSAVER_JPG, PN_SCREENSAVER_JPG_SIZE, 0, 0);
#else
  renderDefaultScreensaver();
#endif
}
void activateScreensaver() { if (setupPortalActive || irActive) return; screensaverActive=true; renderCustomScreensaver(); }
void leaveScreensaver() { if (!screensaverActive) return; screensaverActive=false; lastInteractionAt=millis(); renderMenu(); }
void renderScreenSaverApp() {
  beginFrame(); drawAppHeader("Screen Saver"); drawPageTag(pageIndex,2);
  if (pageIndex==0) {
    fontUI(); frame.setTextColor(ACCENT,BG); drawWrappedText(PN_HAS_CUSTOM_SCREENSAVER ? "Custom image loaded" : "Clock screensaver",8,78,W()-16,17,4,ACCENT);
    fontSmall(); frame.setTextColor(FG,BG); drawWrappedText("Automatic after 60 seconds of no button activity. Any button wakes the device.",8,132,W()-16,14,5,FG);
  } else {
    frame.fillRoundRect(7,70,W()-14,120,8,PANEL); fontSmall(); frame.setTextColor(FG,PANEL);
    drawWrappedText("Your selected portrait image is embedded in firmware at 135x240. Hold A to preview it immediately.",13,82,W()-26,14,8,FG,PANEL);
  }
  frame.setTextColor(MUTED,BG); frame.setCursor(8,199); frame.print("hold A: preview"); drawFooter("A NEXT","B BACK"); present();
}

void renderCurrent(bool action=false) {
  lastRender=millis();
  switch(current) {
    case AppId::Dashboard: renderDashboard(); break;
    case AppId::Stocks: renderPlaceholder("Stock Alerts","Market adapter","Live prices, alerts and buy-zone payloads will come from the Pocket Nexus backend."); break;
    case AppId::VoiceAI: renderPlaceholder("Voice AI","Mic + speaker ready","Push-to-talk STT/TTS will use the backend so model keys are not stored on-device."); break;
    case AppId::DeskPager: renderDeskPager(); break;
    case AppId::Calendar: renderPlaceholder("Calendar","Next event adapter","Private calendar data will be reduced to only the next-event payload needed on the device."); break;
    case AppId::News: renderPlaceholder("RSS / News","Priority feed adapter","Headlines will come from the existing RSS pipeline as compact paged items."); break;
    case AppId::IRAnalyzer: renderIRAnalyzer(); break;
    case AppId::Level: renderLevel(); break;
    case AppId::Clock: renderClock(); break;
    case AppId::Countdown: renderCountdown(); break;
    case AppId::TiltGame: renderTiltGame(); break;
    case AppId::Dice: renderDice(action); break;
    case AppId::WiFiMonitor: renderWiFiMonitor(); break;
    case AppId::ScreenSaver: renderScreenSaverApp(); break;
    default: break;
  }
}

void enterSelected() {
  current=static_cast<AppId>(selected); inApp=true; pageIndex=0;
  if (current==AppId::Dice) diceValue=1+static_cast<int>(esp_random()%6);
  if (current==AppId::TiltGame) { gameX=W()/2.0f; gameY=H()/2.0f; gameVx=gameVy=0; }
  if (current==AppId::IRAnalyzer) startIrAnalyzer();
  renderCurrent(false);
}
void leaveCurrentApp() { if (current==AppId::IRAnalyzer) stopIrAnalyzer(); inApp=false; pageIndex=0; renderMenu(); }
void markInteraction() { lastInteractionAt=millis(); }

void performAppAction() {
  if (current==AppId::WiFiMonitor) { if (!wifiConnected()) tryWiFiConnection(); else { WiFi.disconnect(); delay(50); WiFi.begin(configuredSsid.c_str(),configuredPassword.c_str()); } }
  else if (current==AppId::IRAnalyzer) { irHasFrame=irFrameValid=irRepeatFrame=false; }
  else if (current==AppId::ScreenSaver) { activateScreensaver(); return; }
  else if (current==AppId::TiltGame) { gameX=W()/2.0f; gameY=H()/2.0f; gameVx=gameVy=0; }
  renderCurrent(true);
}

void handleButtons() {
  bool aClick=M5.BtnA.wasClicked(), aHold=M5.BtnA.wasHold(), bClick=M5.BtnB.wasClicked(), bHold=M5.BtnB.wasHold();
  if (screensaverActive) { if (aClick||aHold||bClick||bHold) leaveScreensaver(); return; }
  if (aClick||aHold||bClick||bHold) markInteraction();
  if (!inApp) { if (aClick) { selected=(selected+1)%APP_COUNT; renderMenu(); } if (bClick) enterSelected(); return; }
  if (current==AppId::WiFiMonitor && bHold) { startSetupPortal(); return; }
  if (bClick) { leaveCurrentApp(); return; }
  if (aHold) { performAppAction(); return; }
  if (aClick) {
    if (isPagedApp(current)) { pageIndex=(pageIndex+1)%pageCount(current); renderCurrent(false); }
    else performAppAction();
  }
}

void periodicRefresh() {
  if (setupPortalActive||screensaverActive) return;
  uint32_t now=millis(); if (!irActive && now-lastInteractionAt>=SCREENSAVER_TIMEOUT_MS) { activateScreensaver(); return; }
  if (!inApp) return;
  if (current==AppId::IRAnalyzer) { if (pollIrFrame()) renderIRAnalyzer(); return; }
  if (current==AppId::TiltGame && now-lastRender>=40) { renderTiltGame(); return; }
  if (current==AppId::Level && now-lastRender>=90) { renderLevel(); return; }
  if ((current==AppId::Clock||current==AppId::Dashboard||current==AppId::DeskPager||current==AppId::Countdown||current==AppId::WiFiMonitor) && now-lastSecond>=1000) { lastSecond=now; renderCurrent(false); }
}
void serviceSetupPortal() {
  if (!setupPortalActive) return; setupServer.handleClient();
  if (M5.BtnB.wasClicked() || millis()-setupStartedAt>=SETUP_TIMEOUT_MS) { stopSetupPortal(); markInteraction(); renderMenu(); }
}

} // namespace pn

void setup() {
  auto cfg=M5.config(); M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(0); M5.Display.setTextWrap(false);
  pn::frame.setColorDepth(16); pn::frame.createSprite(M5.Display.width(),M5.Display.height()); pn::frame.setTextWrap(false);
  pn::beginFrame(); pn::drawStatusBar(); pn::fontUI(); pn::frame.setTextColor(pn::ACCENT,pn::BG); pn::frame.drawString("Pocket",8,72); pn::frame.drawString("Nexus",8,96);
  pn::fontSmall(); pn::frame.setTextColor(pn::MUTED,pn::BG); pn::frame.setCursor(8,132); pn::frame.printf("v%s",pn::VERSION); pn::frame.setCursor(8,150); pn::frame.print("Paged UI"); pn::present(); delay(350);
  pn::lastInteractionAt=millis(); pn::connectWiFi(); if (!pn::setupPortalActive) pn::renderMenu();
}
void loop() {
  M5.update(); pn::serviceSetupPortal();
  if (!pn::setupPortalActive) { pn::handleButtons(); pn::periodicRefresh(); }
  delay(5);
}
