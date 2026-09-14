#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>
#include "driver/rmt_rx.h"

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

constexpr const char* VERSION = "0.3";
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

struct AppEntry {
  const char* menuName;
  const char* title;
};

enum class AppId : uint8_t {
  Dashboard,
  Stocks,
  VoiceAI,
  DeskPager,
  Calendar,
  News,
  IRAnalyzer,
  Level,
  Clock,
  Countdown,
  TiltGame,
  Dice,
  WiFiMonitor,
  ScreenSaver,
  Count
};

constexpr AppEntry APPS[] = {
    {"NEXUS", "Pocket Nexus"},
    {"STOCKS", "Stock Alerts"},
    {"VOICE AI", "Voice AI"},
    {"PAGER", "Desk Pager"},
    {"CALENDAR", "Calendar"},
    {"NEWS", "RSS / News"},
    {"IR", "IR Analyzer"},
    {"LEVEL", "Level"},
    {"CLOCK", "NTP Clock"},
    {"COUNTDOWN", "Countdown"},
    {"GAME", "Tilt Game"},
    {"DICE", "Dice"},
    {"WI-FI", "Wi-Fi"},
    {"SAVER", "Screen Saver"},
};

constexpr uint8_t APP_COUNT = static_cast<uint8_t>(AppId::Count);

M5Canvas frame(&M5.Display);
uint8_t selected = 0;
bool inApp = false;
AppId current = AppId::Dashboard;
uint32_t lastRender = 0;
uint32_t lastSecond = 0;
uint32_t lastInteractionAt = 0;
bool screensaverActive = false;
int diceValue = 1;
float gameX = 67.0f;
float gameY = 132.0f;
float gameVx = 0.0f;
float gameVy = 0.0f;

WebServer setupServer(80);
bool setupRoutesInstalled = false;
bool setupPortalActive = false;
uint32_t setupStartedAt = 0;
String setupApName;
String setupApPassword;
String setupToken;
String configuredSsid;
String configuredPassword;

rmt_channel_handle_t irRxChannel = nullptr;
static rmt_symbol_word_t irSymbols[64];
static volatile bool irRxDone = false;
static volatile size_t irSymbolCount = 0;
bool irActive = false;
bool irHasFrame = false;
bool irFrameValid = false;
bool irRepeatFrame = false;
uint32_t irRawData = 0;
uint16_t irAddress = 0;
uint8_t irCommand = 0;

int W() { return frame.width(); }
int H() { return frame.height(); }

String clipText(const String& value, size_t maxChars) {
  if (value.length() <= maxChars) return value;
  if (maxChars <= 3) return value.substring(0, maxChars);
  return value.substring(0, maxChars - 3) + "...";
}

void fontSmall() {
  frame.setTextFont(&fonts::Font0);
  frame.setTextSize(1);
}

void fontUI() {
  frame.setTextFont(&fonts::FreeMonoBold9pt7b);
  frame.setTextSize(1);
}

void fontBig() {
  frame.setTextFont(&fonts::FreeMonoBold9pt7b);
  frame.setTextSize(2);
}

void beginFrame() {
  frame.fillSprite(BG);
  frame.setTextWrap(false);
  frame.setTextColor(FG, BG);
  frame.setTextDatum(top_left);
  fontSmall();
}

void present() {
  frame.pushSprite(0, 0);
}

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool hasWiFiConfig() {
  return configuredSsid.length() > 0;
}

void drawBatteryIcon(int x, int y, int level, bool charging) {
  const int w = 24;
  const int h = 12;
  frame.drawRoundRect(x, y, w, h, 2, FG);
  frame.fillRect(x + w, y + 4, 2, 4, FG);

  int pct = constrain(level, 0, 100);
  int fillW = (w - 4) * pct / 100;
  uint16_t color = pct <= 15 ? TFT_RED : (pct <= 30 ? WARN : OK);
  if (fillW > 0) frame.fillRoundRect(x + 2, y + 2, fillW, h - 4, 1, color);

  if (charging) {
    fontSmall();
    frame.setTextColor(TFT_BLACK, color);
    frame.setCursor(x + 9, y + 2);
    frame.print("+");
  }
}

void drawStatusBar(const char* label = "NEXUS") {
  frame.fillRect(0, 0, W(), 28, BLUE);
  fontSmall();
  frame.setTextColor(FG, BLUE);
  frame.setCursor(5, 9);
  frame.print(label);

  const bool online = wifiConnected();
  frame.fillCircle(75, 14, 4, online ? OK : (setupPortalActive ? WARN : MUTED));

  const int battery = M5.Power.getBatteryLevel();
  const bool charging = static_cast<bool>(M5.Power.isCharging());
  drawBatteryIcon(84, 8, battery < 0 ? 0 : battery, charging);

  frame.setTextColor(FG, BLUE);
  frame.setCursor(112, 9);
  if (battery >= 0) frame.printf("%d", battery);
  else frame.print("--");
}

void drawAppHeader(const char* title) {
  drawStatusBar();
  fontUI();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(clipText(String(title), 11), 7, 35);
  frame.drawFastHLine(7, 58, W() - 14, MUTED);
}

void drawFooter(const char* a = "A ACTION", const char* b = "B BACK") {
  const int y = H() - 27;
  frame.fillRoundRect(5, y + 2, W() - 10, 23, 6, BLUE);
  fontSmall();
  frame.setTextColor(FG, BLUE);
  frame.setCursor(10, y + 10);
  frame.print(a);
  int bx = W() - 7 - static_cast<int>(strlen(b)) * 6;
  frame.setCursor(max(70, bx), y + 10);
  frame.print(b);
}

void drawInfoCard(int y, const char* label, const String& value, uint16_t valueColor = FG) {
  frame.fillRoundRect(7, y, W() - 14, 44, 8, PANEL);
  fontSmall();
  frame.setTextColor(MUTED, PANEL);
  frame.setCursor(13, y + 7);
  frame.print(label);

  fontUI();
  frame.setTextColor(valueColor, PANEL);
  frame.drawString(clipText(value, 10), 13, y + 20);
}

void drawBatteryCard(int y) {
  const int battery = M5.Power.getBatteryLevel();
  const int voltage = M5.Power.getBatteryVoltage();
  const bool charging = static_cast<bool>(M5.Power.isCharging());
  const int pct = battery < 0 ? 0 : constrain(battery, 0, 100);

  frame.fillRoundRect(7, y, W() - 14, 37, 8, PANEL);
  fontSmall();
  frame.setTextColor(MUTED, PANEL);
  frame.setCursor(13, y + 6);
  frame.print(charging ? "BATTERY +CHG" : "BATTERY");
  if (voltage > 0) {
    frame.setCursor(87, y + 6);
    frame.printf("%.1fV", voltage / 1000.0f);
  }

  fontUI();
  frame.setTextColor(pct <= 20 ? WARN : FG, PANEL);
  String pctText = battery >= 0 ? String(battery) + "%" : "--";
  frame.drawString(pctText, 13, y + 14);

  const int barX = 58;
  const int barY = y + 20;
  const int barW = 60;
  frame.drawRoundRect(barX, barY, barW, 9, 2, MUTED);
  const int fillW = (barW - 4) * pct / 100;
  uint16_t color = pct <= 15 ? TFT_RED : (pct <= 30 ? WARN : OK);
  if (fillW > 0) frame.fillRoundRect(barX + 2, barY + 2, fillW, 5, 1, color);
}

bool readTime(struct tm& t) {
  return getLocalTime(&t, 10);
}

String hhmm(bool seconds = false) {
  struct tm t {};
  if (!readTime(t)) return seconds ? "--:--:--" : "--:--";
  char buf[16];
  strftime(buf, sizeof(buf), seconds ? "%H:%M:%S" : "%H:%M", &t);
  return String(buf);
}

String dateLine() {
  struct tm t {};
  if (!readTime(t)) return "Time not synced";
  char buf[32];
  strftime(buf, sizeof(buf), "%a  %b %d", &t);
  return String(buf);
}

String randomString(size_t length, const char* alphabet) {
  String out;
  out.reserve(length);
  const size_t alphabetLen = strlen(alphabet);
  for (size_t i = 0; i < length; ++i) out += alphabet[esp_random() % alphabetLen];
  return out;
}

void loadWiFiCredentials() {
  if (strlen(PN_WIFI_SSID) > 0) {
    configuredSsid = PN_WIFI_SSID;
    configuredPassword = PN_WIFI_PASSWORD;
    return;
  }
  Preferences prefs;
  if (prefs.begin("pocket-nexus", true)) {
    configuredSsid = prefs.getString("ssid", "");
    configuredPassword = prefs.getString("pass", "");
    prefs.end();
  }
}

void saveWiFiCredentials(const String& ssid, const String& password) {
  Preferences prefs;
  if (prefs.begin("pocket-nexus", false)) {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
  }
}

void addSetupSecurityHeaders() {
  setupServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  setupServer.sendHeader("Pragma", "no-cache");
  setupServer.sendHeader("X-Content-Type-Options", "nosniff");
  setupServer.sendHeader("X-Frame-Options", "DENY");
  setupServer.sendHeader("Referrer-Policy", "no-referrer");
  setupServer.sendHeader("Content-Security-Policy",
                         "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
}

String setupPageHtml() {
  String page;
  page.reserve(2200);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Pocket Nexus Setup</title><style>");
  page += F("body{font-family:system-ui;background:#0b0d10;color:#f5f7fa;margin:0;padding:24px}");
  page += F("main{max-width:520px;margin:auto;background:#151920;padding:24px;border-radius:18px}");
  page += F("input,button{box-sizing:border-box;width:100%;font-size:17px;padding:13px;margin:8px 0;border-radius:10px}");
  page += F("input{background:#0f1318;color:white;border:1px solid #39434f}button{border:0;background:#35d0ba;color:#07110f;font-weight:700}");
  page += F("p{color:#aeb8c5;line-height:1.5}</style></head><body><main>");
  page += F("<h2>Pocket Nexus Wi-Fi</h2><p>This page exists only on the temporary StickS3 hotspot. Credentials remain on-device.</p>");
  page += F("<form method='post' action='/save'><input type='hidden' name='token' value='");
  page += setupToken;
  page += F("'><input name='ssid' maxlength='32' placeholder='2.4 GHz Wi-Fi SSID' required>");
  page += F("<input name='password' type='password' maxlength='63' placeholder='Wi-Fi password'>");
  page += F("<button type='submit'>Save & Restart</button></form>");
  page += F("<p>The setup hotspot closes automatically after 10 minutes.</p></main></body></html>");
  return page;
}

void renderSetupPortal() {
  beginFrame();
  drawStatusBar("SETUP");
  fontUI();
  frame.setTextColor(WARN, BG);
  frame.drawString("Wi-Fi Setup", 7, 36);

  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 66);
  frame.print("HOTSPOT");
  fontUI();
  frame.setTextColor(FG, BG);
  frame.drawString(setupApName, 8, 78);

  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 108);
  frame.print("PASSWORD");
  fontUI();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(setupApPassword, 8, 120);

  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 151);
  frame.print("OPEN");
  fontUI();
  frame.setTextColor(FG, BG);
  frame.drawString("192.168.4.1", 8, 163);

  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 195);
  frame.print("1 client / 10 min");
  drawFooter("A -", "B CANCEL");
  present();
}

void installSetupRoutes() {
  if (setupRoutesInstalled) return;
  setupRoutesInstalled = true;

  setupServer.on("/", HTTP_GET, []() {
    addSetupSecurityHeaders();
    setupServer.send(200, "text/html; charset=utf-8", setupPageHtml());
  });

  setupServer.on("/save", HTTP_POST, []() {
    addSetupSecurityHeaders();
    const String token = setupServer.arg("token");
    const String ssid = setupServer.arg("ssid");
    const String password = setupServer.arg("password");

    if (token != setupToken) {
      setupServer.send(403, "text/plain", "Invalid setup token");
      return;
    }
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) {
      setupServer.send(400, "text/plain", "Invalid Wi-Fi credentials");
      return;
    }

    saveWiFiCredentials(ssid, password);
    setupServer.send(200, "text/html; charset=utf-8",
                     "<html><body><h2>Saved</h2><p>Pocket Nexus is restarting.</p></body></html>");
    delay(700);
    ESP.restart();
  });

  setupServer.onNotFound([]() {
    addSetupSecurityHeaders();
    setupServer.sendHeader("Location", "/", true);
    setupServer.send(302, "text/plain", "");
  });
}

void stopSetupPortal() {
  if (!setupPortalActive) return;
  setupServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  setupPortalActive = false;
  setupApPassword = "";
  setupToken = "";
}

void startSetupPortal() {
  if (setupPortalActive) return;

  static const char* safeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  static const char* hexAlphabet = "0123456789abcdef";
  const uint64_t mac = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", static_cast<uint16_t>(mac & 0xFFFF));

  setupApName = String("PN-Setup-") + suffix;
  setupApPassword = randomString(12, safeAlphabet);
  setupToken = randomString(24, hexAlphabet);
  setupStartedAt = millis();

  WiFi.disconnect(true, false);
  delay(50);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(setupApName.c_str(), setupApPassword.c_str(), 1, false, 1);
  setupPortalActive = true;

  installSetupRoutes();
  setupServer.begin();
  renderSetupPortal();
}

void renderConnecting() {
  beginFrame();
  drawAppHeader("Connecting");
  fontUI();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(clipText(configuredSsid, 10), 8, 78);
  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 112);
  frame.print("Trying saved Wi-Fi...");
  present();
}

bool tryWiFiConnection() {
  if (!hasWiFiConfig()) return false;

  stopSetupPortal();
  WiFi.mode(WIFI_STA);
  WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());
  renderConnecting();

  const uint32_t started = millis();
  while (!wifiConnected() && millis() - started < 8000) {
    M5.update();
    delay(50);
  }

  if (!wifiConnected()) return false;
  configTzTime(PN_TIMEZONE, "pool.ntp.org", "time.nist.gov");
  return true;
}

void connectWiFi() {
  loadWiFiCredentials();
  if (!hasWiFiConfig()) {
    startSetupPortal();
    return;
  }
  tryWiFiConnection();
}

void renderMenu() {
  beginFrame();
  drawStatusBar();
  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(7, 34);
  frame.printf("APPS %02u/%02u", selected + 1, APP_COUNT);

  constexpr uint8_t rows = 4;
  uint8_t start = selected >= rows ? selected - rows + 1 : 0;

  for (uint8_t row = 0; row < rows; ++row) {
    const uint8_t idx = start + row;
    if (idx >= APP_COUNT) break;
    const int y = 48 + row * 39;
    const bool active = idx == selected;
    frame.fillRoundRect(5, y, W() - 10, 34, 7, active ? BLUE : PANEL2);
    if (!active) frame.drawRoundRect(5, y, W() - 10, 34, 7, MUTED);

    fontSmall();
    frame.setTextColor(active ? FG : MUTED, active ? BLUE : PANEL2);
    frame.setCursor(10, y + 13);
    frame.printf("%02u", idx + 1);

    fontUI();
    frame.setTextColor(FG, active ? BLUE : PANEL2);
    frame.drawString(APPS[idx].menuName, 34, y + 7);
  }

  drawFooter("A NEXT", "B OPEN");
  present();
}

void renderDashboard() {
  beginFrame();
  drawAppHeader("Pocket Nexus");

  fontBig();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(hhmm(), 8, 65);

  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(9, 108);
  frame.print(dateLine());

  String wifiLine = wifiConnected() ? String(WiFi.RSSI()) + " dBm" : "Offline";
  drawInfoCard(126, "WI-FI", wifiLine, wifiConnected() ? OK : WARN);
  drawBatteryCard(174);
  drawFooter("A REFRESH", "B BACK");
  present();
}

void renderPlaceholder(const char* title, const char* line1, const char* line2) {
  beginFrame();
  drawAppHeader(title);
  frame.fillRoundRect(7, 72, W() - 14, 102, 9, PANEL);
  fontUI();
  frame.setTextColor(ACCENT, PANEL);
  frame.drawString(clipText(String(line1), 10), 13, 84);
  fontSmall();
  frame.setTextColor(FG, PANEL);
  frame.setCursor(13, 118);
  frame.print(line2);
  frame.setTextColor(MUTED, PANEL);
  frame.setCursor(13, 145);
  frame.print(strlen(PN_API_BASE_URL) ? "API configured" : "Backend next");
  drawFooter("A REFRESH", "B BACK");
  present();
}

void renderDeskPager() {
  beginFrame();
  drawAppHeader("Desk Pager");
  fontBig();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(hhmm(), 8, 67);
  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(9, 111);
  frame.print(dateLine());
  drawInfoCard(132, "NETWORK", wifiConnected() ? "Online" : "Offline", wifiConnected() ? OK : WARN);
  drawBatteryCard(174);
  drawFooter("A REFRESH", "B BACK");
  present();
}

void readAccel(float& ax, float& ay, float& az) {
  ax = ay = 0.0f;
  az = 1.0f;
  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    ax = d.accel.x;
    ay = d.accel.y;
    az = d.accel.z;
  }
}

void renderLevel() {
  float ax, ay, az;
  readAccel(ax, ay, az);
  const float roll = atan2f(ay, az) * 180.0f / PI;
  const float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI;

  beginFrame();
  drawAppHeader("Level");

  fontSmall();
  frame.setTextColor(FG, BG);
  frame.setCursor(8, 70);
  frame.printf("Pitch %5.1f", pitch);
  frame.setCursor(8, 86);
  frame.printf("Roll  %5.1f", roll);

  const int cx = W() / 2;
  const int cy = 151;
  frame.drawCircle(cx, cy, 42, MUTED);
  frame.drawCircle(cx, cy, 21, MUTED);
  frame.drawFastHLine(cx - 38, cy, 76, MUTED);
  frame.drawFastVLine(cx, cy - 38, 76, MUTED);
  const int bx = constrain(cx + static_cast<int>(ay * 37.0f), cx - 37, cx + 37);
  const int by = constrain(cy - static_cast<int>(ax * 37.0f), cy - 37, cy + 37);
  frame.fillCircle(bx, by, 7, ACCENT);

  drawFooter("A ZERO", "B BACK");
  present();
}

void renderClock() {
  beginFrame();
  drawAppHeader("NTP Clock");
  fontBig();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(hhmm(), 8, 78);
  fontUI();
  frame.setTextColor(FG, BG);
  frame.drawString(dateLine(), 8, 125);
  fontSmall();
  frame.setTextColor(wifiConnected() ? OK : WARN, BG);
  frame.setCursor(8, 160);
  frame.print(wifiConnected() ? "NTP synced" : "Offline / unsynced");
  drawBatteryCard(174);
  drawFooter("A REFRESH", "B BACK");
  present();
}

void renderCountdown() {
  beginFrame();
  drawAppHeader("Countdown");
  fontUI();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(clipText(String(PN_COUNTDOWN_LABEL), 10), 8, 72);

  if (PN_COUNTDOWN_EPOCH <= 0) {
    fontUI();
    frame.setTextColor(WARN, BG);
    frame.drawString("Not set", 8, 115);
    fontSmall();
    frame.setTextColor(MUTED, BG);
    frame.setCursor(8, 150);
    frame.print("Remote sync is next.");
  } else {
    const time_t now = time(nullptr);
    long long delta = static_cast<long long>(PN_COUNTDOWN_EPOCH) - static_cast<long long>(now);
    const bool overdue = delta < 0;
    if (overdue) delta = -delta;
    const long long days = delta / 86400LL;
    const int hours = (delta % 86400LL) / 3600LL;
    fontBig();
    frame.setTextColor(FG, BG);
    frame.drawString(String(days) + "d", 8, 112);
    fontUI();
    frame.drawString(String(hours) + "h " + (overdue ? "late" : "left"), 8, 160);
  }
  drawFooter("A REFRESH", "B BACK");
  present();
}

void renderDice(bool rollNow) {
  if (rollNow) {
    diceValue = 1 + static_cast<int>(esp_random() % 6);
    M5.Speaker.tone(2200, 40);
  }

  beginFrame();
  drawAppHeader("Dice");
  frame.fillRoundRect(16, 78, W() - 32, 108, 16, PANEL);
  fontBig();
  frame.setTextSize(3);
  frame.setTextColor(ACCENT, PANEL);
  frame.setTextDatum(middle_center);
  frame.drawString(String(diceValue), W() / 2, 132);
  frame.setTextDatum(top_left);
  drawFooter("A ROLL", "B BACK");
  present();
}

void renderWiFiMonitor() {
  beginFrame();
  drawAppHeader("Wi-Fi");

  if (wifiConnected()) {
    drawInfoCard(68, "STATUS", "Connected", OK);
    drawInfoCard(116, "SSID", clipText(WiFi.SSID(), 10));
    drawInfoCard(164, "SIGNAL", String(WiFi.RSSI()) + " dBm");
    fontSmall();
    frame.setTextColor(MUTED, BG);
    frame.setCursor(10, 207);
    frame.print(WiFi.localIP().toString());
  } else {
    drawInfoCard(68, "STATUS", hasWiFiConfig() ? "Disconnected" : "Not set", WARN);
    drawInfoCard(116, "SAVED SSID", hasWiFiConfig() ? clipText(configuredSsid, 10) : "None");
    fontSmall();
    frame.setTextColor(MUTED, BG);
    frame.setCursor(9, 171);
    frame.print("A reconnect");
    frame.setCursor(9, 189);
    frame.print("Hold B: secure setup");
  }
  drawFooter("A RETRY", "B BACK");
  present();
}

void renderTiltGame() {
  float ax, ay, az;
  readAccel(ax, ay, az);

  gameVx = constrain(gameVx + ay * 0.28f, -3.5f, 3.5f);
  gameVy = constrain(gameVy - ax * 0.28f, -3.5f, 3.5f);
  gameVx *= 0.96f;
  gameVy *= 0.96f;
  gameX = constrain(gameX + gameVx, 14.0f, static_cast<float>(W() - 14));
  gameY = constrain(gameY + gameVy, 76.0f, static_cast<float>(H() - 45));

  beginFrame();
  drawAppHeader("Tilt Game");
  frame.drawRoundRect(7, 66, W() - 14, H() - 109, 8, MUTED);
  frame.fillCircle(static_cast<int>(gameX), static_cast<int>(gameY), 7, ACCENT);
  drawFooter("A RESET", "B BACK");
  present();
}

bool irRxDoneCallback(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata, void*) {
  irSymbolCount = edata->num_symbols;
  irRxDone = true;
  return true;
}

bool decodeNEC(const rmt_symbol_word_t* symbols, size_t count, uint32_t* outRaw, bool* outRepeat) {
  *outRaw = 0;
  *outRepeat = false;
  if (count < 2) return false;

  uint32_t headerLow = symbols[0].duration0;
  uint32_t headerHigh = symbols[0].duration1;

  if (headerLow > 8000 && headerHigh > 4000) {
    if (count < 33) return false;
  } else if (headerLow > 8000 && headerHigh > 2000 && headerHigh < 3000) {
    *outRepeat = true;
    return false;
  } else {
    return false;
  }

  for (int i = 0; i < 32; ++i) {
    uint32_t mark = symbols[i + 1].duration0;
    uint32_t space = symbols[i + 1].duration1;
    if (mark < 300 || mark > 800) return false;
    if (space > 1000) *outRaw |= (1UL << i);
  }

  uint8_t cmd = (*outRaw >> 16) & 0xFF;
  uint8_t cmdInv = (*outRaw >> 24) & 0xFF;
  return (cmd ^ cmdInv) == 0xFF;
}

void startIrReceive() {
  if (!irRxChannel) return;
  rmt_receive_config_t cfg = {
      .signal_range_min_ns = 1000,
      .signal_range_max_ns = 20000000,
  };
  rmt_receive(irRxChannel, irSymbols, sizeof(irSymbols), &cfg);
}

bool startIrAnalyzer() {
  if (irActive) return true;

  M5.Speaker.end();
  M5.Power.setExtOutput(true, m5::ext_none);

  rmt_rx_channel_config_t rxCfg = {
      .gpio_num = static_cast<gpio_num_t>(IR_RECEIVE_PIN),
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000000,
      .mem_block_symbols = 128,
  };

  if (rmt_new_rx_channel(&rxCfg, &irRxChannel) != ESP_OK) {
    irRxChannel = nullptr;
    return false;
  }

  rmt_rx_event_callbacks_t cbs = {
      .on_recv_done = irRxDoneCallback,
  };
  if (rmt_rx_register_event_callbacks(irRxChannel, &cbs, nullptr) != ESP_OK ||
      rmt_enable(irRxChannel) != ESP_OK) {
    rmt_del_channel(irRxChannel);
    irRxChannel = nullptr;
    return false;
  }

  irRxDone = false;
  irSymbolCount = 0;
  irHasFrame = false;
  irActive = true;
  startIrReceive();
  return true;
}

void stopIrAnalyzer() {
  if (!irActive) return;
  if (irRxChannel) {
    rmt_disable(irRxChannel);
    rmt_del_channel(irRxChannel);
    irRxChannel = nullptr;
  }
  M5.Power.setExtOutput(false, m5::ext_none);
  M5.Speaker.begin();
  irActive = false;
  irRxDone = false;
}

void processIrFrame() {
  if (!irActive || !irRxDone) return;
  irRxDone = false;

  uint32_t raw = 0;
  bool repeat = false;
  bool valid = decodeNEC(irSymbols, irSymbolCount, &raw, &repeat);

  irHasFrame = true;
  irFrameValid = valid;
  irRepeatFrame = repeat;
  irRawData = raw;

  if (valid) {
    irAddress = raw & 0xFFFF;
    irCommand = (raw >> 16) & 0xFF;
  }

  startIrReceive();
}

void renderIRAnalyzer() {
  beginFrame();
  drawAppHeader("IR Analyzer");

  fontSmall();
  frame.setTextColor(irActive ? OK : WARN, BG);
  frame.setCursor(8, 69);
  frame.print(irActive ? "Listening on GPIO42 / RMT" : "Receiver unavailable");

  if (!irHasFrame) {
    fontUI();
    frame.setTextColor(ACCENT, BG);
    frame.drawString("Point remote", 8, 102);
    fontSmall();
    frame.setTextColor(MUTED, BG);
    frame.setCursor(8, 133);
    frame.print("Press a remote key.");
    frame.setCursor(8, 151);
    frame.print("Keep >30 cm away.");
  } else if (irRepeatFrame) {
    fontUI();
    frame.setTextColor(WARN, BG);
    frame.drawString("NEC REPEAT", 8, 104);
    fontSmall();
    frame.setTextColor(MUTED, BG);
    frame.setCursor(8, 140);
    frame.printf("%u symbols", static_cast<unsigned>(irSymbolCount));
  } else if (irFrameValid) {
    fontUI();
    frame.setTextColor(OK, BG);
    frame.drawString("NEC OK", 8, 94);
    fontSmall();
    frame.setTextColor(FG, BG);
    frame.setCursor(8, 128);
    frame.printf("Addr 0x%04X", irAddress);
    frame.setCursor(8, 146);
    frame.printf("Cmd  0x%02X", irCommand);
    frame.setCursor(8, 164);
    frame.printf("Raw  %08lX", static_cast<unsigned long>(irRawData));
  } else {
    fontUI();
    frame.setTextColor(WARN, BG);
    frame.drawString("RAW SIGNAL", 8, 104);
    fontSmall();
    frame.setTextColor(FG, BG);
    frame.setCursor(8, 140);
    frame.printf("%u symbols", static_cast<unsigned>(irSymbolCount));
    frame.setCursor(8, 158);
    frame.print("Not NEC / decode fail");
  }

  drawFooter("A CLEAR", "B BACK");
  present();
}

void renderDefaultScreensaver() {
  beginFrame();

  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 10);
  frame.print("POCKET NEXUS");

  fontBig();
  frame.setTextColor(FG, BG);
  frame.setTextDatum(middle_center);
  frame.drawString(hhmm(), W() / 2, 92);

  fontUI();
  frame.setTextColor(ACCENT, BG);
  frame.drawString(dateLine(), W() / 2, 132);

  const int battery = M5.Power.getBatteryLevel();
  const bool charging = static_cast<bool>(M5.Power.isCharging());
  drawBatteryIcon((W() - 24) / 2, 166, battery < 0 ? 0 : battery, charging);

  fontSmall();
  frame.setTextColor(MUTED, BG);
  frame.drawString("press any key", W() / 2, 207);
  frame.setTextDatum(top_left);
  present();
}

void renderCustomScreensaver() {
#if PN_HAS_CUSTOM_SCREENSAVER
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.pushImage(0, 0, PN_SCREENSAVER_WIDTH, PN_SCREENSAVER_HEIGHT, PN_SCREENSAVER_RGB565);
#else
  renderDefaultScreensaver();
#endif
}

void activateScreensaver() {
  if (setupPortalActive || irActive) return;
  screensaverActive = true;
  renderCustomScreensaver();
}

void leaveScreensaver() {
  if (!screensaverActive) return;
  screensaverActive = false;
  lastInteractionAt = millis();
  renderMenu();
}

void renderScreenSaverApp() {
  beginFrame();
  drawAppHeader("Screen Saver");
  fontUI();
  frame.setTextColor(ACCENT, BG);
#if PN_HAS_CUSTOM_SCREENSAVER
  frame.drawString("Custom image", 8, 82);
#else
  frame.drawString("Clock mode", 8, 82);
#endif
  fontSmall();
  frame.setTextColor(FG, BG);
  frame.setCursor(8, 119);
  frame.print("A: start now");
  frame.setCursor(8, 139);
  frame.print("Auto: 60 sec idle");
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 169);
#if PN_HAS_CUSTOM_SCREENSAVER
  frame.print("135x240 image loaded");
#else
  frame.print("Send me an image to");
  frame.setCursor(8, 185);
  frame.print("embed it in firmware.");
#endif
  drawFooter("A START", "B BACK");
  present();
}

void renderCurrent(bool action = false) {
  lastRender = millis();
  switch (current) {
    case AppId::Dashboard: renderDashboard(); break;
    case AppId::Stocks: renderPlaceholder("Stock Alerts", "Market adapter", "Live endpoint next"); break;
    case AppId::VoiceAI: renderPlaceholder("Voice AI", "Mic + speaker", "STT / TTS next"); break;
    case AppId::DeskPager: renderDeskPager(); break;
    case AppId::Calendar: renderPlaceholder("Calendar", "Next event", "Secure adapter next"); break;
    case AppId::News: renderPlaceholder("RSS / News", "Priority feed", "Feed endpoint next"); break;
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
  current = static_cast<AppId>(selected);
  inApp = true;

  if (current == AppId::Dice) diceValue = 1 + static_cast<int>(esp_random() % 6);
  if (current == AppId::TiltGame) {
    gameX = W() / 2.0f;
    gameY = H() / 2.0f;
    gameVx = gameVy = 0.0f;
  }
  if (current == AppId::IRAnalyzer) startIrAnalyzer();

  renderCurrent(false);
}

void leaveCurrentApp() {
  if (current == AppId::IRAnalyzer) stopIrAnalyzer();
  inApp = false;
  renderMenu();
}

void markInteraction() {
  lastInteractionAt = millis();
}

void handleButtons() {
  const bool aClick = M5.BtnA.wasClicked();
  const bool bClick = M5.BtnB.wasClicked();
  const bool bHold = M5.BtnB.wasHold();

  if (screensaverActive) {
    if (aClick || bClick || bHold) leaveScreensaver();
    return;
  }

  if (aClick || bClick || bHold) markInteraction();

  if (!inApp) {
    if (aClick) {
      selected = (selected + 1) % APP_COUNT;
      renderMenu();
    }
    if (bClick) enterSelected();
    return;
  }

  if (current == AppId::WiFiMonitor && bHold) {
    startSetupPortal();
    return;
  }

  if (bClick) {
    leaveCurrentApp();
    return;
  }

  if (aClick) {
    if (current == AppId::WiFiMonitor && !wifiConnected()) tryWiFiConnection();
    if (current == AppId::TiltGame) {
      gameX = W() / 2.0f;
      gameY = H() / 2.0f;
      gameVx = gameVy = 0.0f;
    }
    if (current == AppId::IRAnalyzer) {
      irHasFrame = false;
      irFrameValid = false;
      irRepeatFrame = false;
    }
    if (current == AppId::ScreenSaver) {
      activateScreensaver();
      return;
    }
    renderCurrent(true);
  }
}

void periodicRefresh() {
  if (setupPortalActive || screensaverActive) return;

  const uint32_t now = millis();
  if (!irActive && now - lastInteractionAt >= SCREENSAVER_TIMEOUT_MS) {
    activateScreensaver();
    return;
  }

  if (!inApp) return;

  if (current == AppId::IRAnalyzer) {
    if (irRxDone) {
      processIrFrame();
      renderIRAnalyzer();
    }
    return;
  }

  if (current == AppId::TiltGame && now - lastRender >= 40) {
    renderTiltGame();
    return;
  }

  if (current == AppId::Level && now - lastRender >= 90) {
    renderLevel();
    return;
  }

  if ((current == AppId::Clock || current == AppId::Dashboard || current == AppId::DeskPager ||
       current == AppId::Countdown || current == AppId::WiFiMonitor) &&
      now - lastSecond >= 1000) {
    lastSecond = now;
    renderCurrent(false);
  }
}

void serviceSetupPortal() {
  if (!setupPortalActive) return;
  setupServer.handleClient();

  if (M5.BtnB.wasClicked() || millis() - setupStartedAt >= SETUP_TIMEOUT_MS) {
    stopSetupPortal();
    markInteraction();
    renderMenu();
  }
}

}  // namespace pn

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  M5.Display.setRotation(0);
  M5.Display.setTextWrap(false);

  pn::frame.setColorDepth(16);
  pn::frame.createSprite(M5.Display.width(), M5.Display.height());
  pn::frame.setTextWrap(false);

  pn::beginFrame();
  pn::drawStatusBar();
  pn::fontUI();
  pn::frame.setTextColor(pn::ACCENT, pn::BG);
  pn::frame.drawString("Pocket", 8, 72);
  pn::frame.drawString("Nexus", 8, 96);
  pn::fontSmall();
  pn::frame.setTextColor(pn::MUTED, pn::BG);
  pn::frame.setCursor(8, 132);
  pn::frame.printf("v%s", pn::VERSION);
  pn::frame.setCursor(8, 150);
  pn::frame.print("Portrait UI");
  pn::present();
  delay(350);

  pn::lastInteractionAt = millis();
  pn::connectWiFi();
  if (!pn::setupPortalActive) pn::renderMenu();
}

void loop() {
  M5.update();
  pn::serviceSetupPortal();
  if (!pn::setupPortalActive) {
    pn::handleButtons();
    pn::periodicRefresh();
  }
  delay(5);
}
