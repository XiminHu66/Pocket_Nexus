#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>

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

namespace pn {

constexpr const char* VERSION = "0.2";
constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t FG = TFT_WHITE;
constexpr uint16_t MUTED = TFT_DARKGREY;
constexpr uint16_t ACCENT = TFT_CYAN;
constexpr uint16_t OK = TFT_GREEN;
constexpr uint16_t WARN = TFT_YELLOW;
constexpr uint16_t PANEL = TFT_NAVY;
constexpr uint32_t SETUP_TIMEOUT_MS = 10UL * 60UL * 1000UL;

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
  Count
};

constexpr AppEntry APPS[] = {
    {"NEXUS", "Pocket Nexus"},
    {"STOCKS", "Stock Alerts"},
    {"VOICE AI", "AI Push-to-Talk"},
    {"PAGER", "Desk Pager"},
    {"CALENDAR", "Next Calendar"},
    {"NEWS", "RSS / News"},
    {"IR", "IR Analyzer"},
    {"LEVEL", "Level / Attitude"},
    {"CLOCK", "NTP Clock"},
    {"COUNTDOWN", "Countdown"},
    {"GAME", "Tilt Game"},
    {"DICE", "Dice / Random"},
    {"WI-FI", "Wi-Fi Monitor"},
};

constexpr uint8_t APP_COUNT = static_cast<uint8_t>(AppId::Count);

M5Canvas frame(&M5.Display);
uint8_t selected = 0;
bool inApp = false;
AppId current = AppId::Dashboard;
uint32_t lastRender = 0;
uint32_t lastSecond = 0;
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

int W() { return frame.width(); }
int H() { return frame.height(); }

String clipText(const String& value, size_t maxChars) {
  if (value.length() <= maxChars) return value;
  if (maxChars <= 3) return value.substring(0, maxChars);
  return value.substring(0, maxChars - 3) + "...";
}

void beginFrame() {
  frame.fillSprite(BG);
  frame.setTextWrap(false);
  frame.setTextColor(FG, BG);
  frame.setTextSize(1.0f);
  frame.setTextDatum(top_left);
}

void present() {
  frame.pushSprite(0, 0);
}

void drawStatusBar(const char* label = "POCKET NEXUS") {
  frame.fillRect(0, 0, W(), 22, PANEL);
  frame.setTextColor(FG, PANEL);
  frame.setTextSize(1.0f);
  frame.setCursor(5, 7);
  frame.print(label);

  const int battery = M5.Power.getBatteryLevel();
  const bool online = WiFi.status() == WL_CONNECTED;
  frame.fillCircle(W() - 35, 11, 3, online ? OK : (setupPortalActive ? WARN : MUTED));
  frame.setCursor(W() - 28, 7);
  if (battery >= 0) {
    frame.printf("%d%%", battery);
  } else {
    frame.print("--");
  }
  frame.setTextColor(FG, BG);
}

void drawAppHeader(const char* title) {
  drawStatusBar();
  frame.setTextColor(ACCENT, BG);
  frame.setTextSize(1.45f);
  frame.setCursor(7, 31);
  frame.print(title);
  frame.setTextSize(1.0f);
  frame.setTextColor(FG, BG);
  frame.drawFastHLine(7, 52, W() - 14, TFT_DARKGREY);
}

void drawFooter(const char* a = "A ACTION", const char* b = "B BACK") {
  const int y = H() - 27;
  frame.fillRoundRect(5, y + 2, W() - 10, 23, 6, PANEL);
  frame.setTextSize(1.0f);
  frame.setTextColor(ACCENT, PANEL);
  frame.setCursor(10, y + 10);
  frame.print(a);
  frame.setTextColor(FG, PANEL);
  frame.setCursor(W() - 51, y + 10);
  frame.print(b);
  frame.setTextColor(FG, BG);
}

void drawInfoCard(int y, const char* label, const String& value, uint16_t valueColor = FG) {
  frame.fillRoundRect(7, y, W() - 14, 42, 7, PANEL);
  frame.setTextSize(0.9f);
  frame.setTextColor(MUTED, PANEL);
  frame.setCursor(13, y + 7);
  frame.print(label);
  frame.setTextSize(1.25f);
  frame.setTextColor(valueColor, PANEL);
  frame.setCursor(13, y + 21);
  frame.print(clipText(value, 14));
  frame.setTextColor(FG, BG);
}

bool hasWiFiConfig() {
  return configuredSsid.length() > 0;
}

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool readTime(struct tm& t) {
  return getLocalTime(&t, 10);
}

String hhmm(bool seconds = false) {
  struct tm t {};
  if (!readTime(t)) return "--:--";
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
  for (size_t i = 0; i < length; ++i) {
    out += alphabet[esp_random() % alphabetLen];
  }
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
  page += F("<h2>Pocket Nexus Wi-Fi</h2><p>This setup page exists only on the temporary StickS3 hotspot. Credentials remain on the device.</p>");
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
  drawStatusBar("SECURE SETUP");
  frame.setTextColor(WARN, BG);
  frame.setTextSize(1.45f);
  frame.setCursor(7, 31);
  frame.print("Wi-Fi Setup");
  frame.setTextSize(0.9f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 58);
  frame.print("HOTSPOT");
  frame.setTextSize(1.15f);
  frame.setTextColor(FG, BG);
  frame.setCursor(8, 72);
  frame.print(setupApName);
  frame.setTextSize(0.9f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 99);
  frame.print("PASSWORD");
  frame.setTextSize(1.15f);
  frame.setTextColor(ACCENT, BG);
  frame.setCursor(8, 113);
  frame.print(setupApPassword);
  frame.setTextSize(0.9f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 142);
  frame.print("OPEN");
  frame.setTextSize(1.2f);
  frame.setTextColor(FG, BG);
  frame.setCursor(8, 156);
  frame.print("192.168.4.1");
  frame.setTextSize(0.9f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 184);
  frame.print("1 client / 10 min timeout");
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
    addSetupSecurityHeaders();
    setupServer.send(200, "text/html; charset=utf-8",
                     "<html><body style='font-family:system-ui;background:#111;color:#fff'><h2>Saved</h2><p>Pocket Nexus is restarting.</p></body></html>");
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
  frame.setTextColor(ACCENT, BG);
  frame.setTextSize(1.35f);
  frame.setCursor(8, 78);
  frame.print(clipText(configuredSsid, 14));
  frame.setTextSize(1.0f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, 108);
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
  frame.setTextColor(MUTED, BG);
  frame.setTextSize(0.9f);
  frame.setCursor(7, 27);
  frame.printf("APPS  %02u / %02u", selected + 1, APP_COUNT);

  constexpr uint8_t rows = 4;
  uint8_t start = 0;
  if (selected >= rows) start = selected - rows + 1;

  for (uint8_t row = 0; row < rows; ++row) {
    const uint8_t idx = start + row;
    if (idx >= APP_COUNT) break;
    const int y = 42 + row * 42;
    const bool active = idx == selected;

    if (active) {
      frame.fillRoundRect(5, y, W() - 10, 35, 7, ACCENT);
      frame.setTextColor(TFT_BLACK, ACCENT);
    } else {
      frame.drawRoundRect(5, y, W() - 10, 35, 7, TFT_DARKGREY);
      frame.setTextColor(FG, BG);
    }

    frame.setTextSize(1.25f);
    frame.setCursor(11, y + 11);
    frame.printf("%02u", idx + 1);
    frame.setCursor(39, y + 11);
    frame.print(APPS[idx].menuName);
  }

  drawFooter("A NEXT", "B OPEN");
  present();
}

void renderDashboard() {
  beginFrame();
  drawAppHeader("Pocket Nexus");

  frame.setTextColor(ACCENT, BG);
  frame.setTextSize(2.35f);
  frame.setCursor(8, 62);
  frame.print(hhmm());

  frame.setTextSize(1.0f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(9, 92);
  frame.print(dateLine());

  String wifiLine = wifiConnected() ? String(WiFi.RSSI()) + " dBm" : "Offline";
  drawInfoCard(112, "WI-FI", wifiLine, wifiConnected() ? OK : WARN);

  const int battery = M5.Power.getBatteryLevel();
  drawInfoCard(158, "BATTERY", battery >= 0 ? String(battery) + "%" : "Unknown", battery >= 20 ? FG : WARN);

  drawFooter("A REFRESH", "B BACK");
  present();
}

void renderPlaceholder(const char* title, const char* line1, const char* line2) {
  beginFrame();
  drawAppHeader(title);
  frame.fillRoundRect(7, 70, W() - 14, 98, 9, PANEL);
  frame.setTextColor(ACCENT, PANEL);
  frame.setTextSize(1.2f);
  frame.setCursor(13, 84);
  frame.print(line1);
  frame.setTextColor(FG, PANEL);
  frame.setTextSize(1.0f);
  frame.setCursor(13, 112);
  frame.print(line2);
  frame.setTextColor(MUTED, PANEL);
  frame.setCursor(13, 142);
  frame.print(strlen(PN_API_BASE_URL) ? "API configured" : "Backend next");
  drawFooter("A REFRESH", "B BACK");
  present();
}

void renderDeskPager() {
  beginFrame();
  drawAppHeader("Desk Pager");
  frame.setTextColor(ACCENT, BG);
  frame.setTextSize(2.4f);
  frame.setCursor(8, 65);
  frame.print(hhmm());
  frame.setTextSize(1.0f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(9, 96);
  frame.print(dateLine());

  drawInfoCard(119, "NETWORK", wifiConnected() ? "Online" : "Offline", wifiConnected() ? OK : WARN);
  drawInfoCard(165, "SIGNAL", wifiConnected() ? String(WiFi.RSSI()) + " dBm" : "--");
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
  frame.setTextSize(1.05f);
  frame.setTextColor(FG, BG);
  frame.setCursor(8, 64);
  frame.printf("Pitch %5.1f", pitch);
  frame.setCursor(8, 82);
  frame.printf("Roll  %5.1f", roll);

  const int cx = W() / 2;
  const int cy = 146;
  frame.drawCircle(cx, cy, 43, TFT_DARKGREY);
  frame.drawCircle(cx, cy, 22, TFT_DARKGREY);
  frame.drawFastHLine(cx - 39, cy, 78, MUTED);
  frame.drawFastVLine(cx, cy - 39, 78, MUTED);
  const int bx = constrain(cx + static_cast<int>(ay * 38.0f), cx - 38, cx + 38);
  const int by = constrain(cy - static_cast<int>(ax * 38.0f), cy - 38, cy + 38);
  frame.fillCircle(bx, by, 7, ACCENT);
  drawFooter("A HOLD", "B BACK");
  present();
}

void renderClock() {
  beginFrame();
  drawAppHeader("NTP Clock");
  frame.setTextColor(ACCENT, BG);
  frame.setTextSize(2.15f);
  frame.setCursor(7, 78);
  frame.print(hhmm(true));
  frame.setTextSize(1.15f);
  frame.setTextColor(FG, BG);
  frame.setCursor(8, 118);
  frame.print(dateLine());
  frame.setTextSize(1.0f);
  frame.setTextColor(wifiConnected() ? OK : WARN, BG);
  frame.setCursor(8, 153);
  frame.print(wifiConnected() ? "NTP synced" : "Offline / unsynced");
  drawFooter("A REFRESH", "B BACK");
  present();
}

void renderCountdown() {
  beginFrame();
  drawAppHeader("Countdown");
  frame.setTextColor(ACCENT, BG);
  frame.setTextSize(1.2f);
  frame.setCursor(8, 70);
  frame.print(clipText(String(PN_COUNTDOWN_LABEL), 15));

  if (PN_COUNTDOWN_EPOCH <= 0) {
    frame.setTextColor(WARN, BG);
    frame.setTextSize(1.4f);
    frame.setCursor(8, 112);
    frame.print("Not configured");
    frame.setTextColor(MUTED, BG);
    frame.setTextSize(1.0f);
    frame.setCursor(8, 145);
    frame.print("Remote countdown sync");
    frame.setCursor(8, 162);
    frame.print("is planned next.");
  } else {
    const time_t now = time(nullptr);
    long long delta = static_cast<long long>(PN_COUNTDOWN_EPOCH) - static_cast<long long>(now);
    const bool overdue = delta < 0;
    if (overdue) delta = -delta;
    const long long days = delta / 86400LL;
    const int hours = (delta % 86400LL) / 3600LL;
    frame.setTextColor(FG, BG);
    frame.setTextSize(2.0f);
    frame.setCursor(8, 112);
    frame.printf("%lldd", days);
    frame.setTextSize(1.3f);
    frame.setCursor(8, 150);
    frame.printf("%02dh %s", hours, overdue ? "late" : "left");
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
  frame.fillRoundRect(18, 74, W() - 36, 105, 16, PANEL);
  frame.setTextColor(ACCENT, PANEL);
  frame.setTextSize(6.0f);
  frame.setCursor(49, 96);
  frame.print(diceValue);
  drawFooter("A ROLL", "B BACK");
  present();
}

void renderWiFiMonitor() {
  beginFrame();
  drawAppHeader("Wi-Fi");

  if (wifiConnected()) {
    drawInfoCard(64, "STATUS", "Connected", OK);
    drawInfoCard(110, "SSID", clipText(WiFi.SSID(), 14));
    drawInfoCard(156, "SIGNAL / IP", String(WiFi.RSSI()) + " dBm");
    frame.setTextSize(0.85f);
    frame.setTextColor(MUTED, BG);
    frame.setCursor(10, 202);
    frame.print(clipText(WiFi.localIP().toString(), 18));
  } else {
    drawInfoCard(64, "STATUS", hasWiFiConfig() ? "Disconnected" : "Not configured", WARN);
    drawInfoCard(110, "SAVED SSID", hasWiFiConfig() ? clipText(configuredSsid, 14) : "None");
    frame.setTextSize(0.9f);
    frame.setTextColor(MUTED, BG);
    frame.setCursor(9, 164);
    frame.print("A: reconnect");
    frame.setCursor(9, 181);
    frame.print("Hold B: secure setup");
  }
  drawFooter("A RETRY", "B BACK");
  present();
}

void renderTiltGame() {
  float ax, ay, az;
  readAccel(ax, ay, az);

  // Portrait transform: front blue button sits at the bottom of the screen.
  gameVx = constrain(gameVx + ay * 0.28f, -3.5f, 3.5f);
  gameVy = constrain(gameVy - ax * 0.28f, -3.5f, 3.5f);
  gameVx *= 0.96f;
  gameVy *= 0.96f;
  gameX = constrain(gameX + gameVx, 14.0f, static_cast<float>(W() - 14));
  gameY = constrain(gameY + gameVy, 72.0f, static_cast<float>(H() - 43));

  beginFrame();
  drawAppHeader("Tilt Game");
  frame.drawRoundRect(7, 62, W() - 14, H() - 103, 8, TFT_DARKGREY);
  frame.fillCircle(static_cast<int>(gameX), static_cast<int>(gameY), 7, ACCENT);
  frame.setTextSize(0.85f);
  frame.setTextColor(MUTED, BG);
  frame.setCursor(8, H() - 38);
  frame.print("Tilt to move");
  drawFooter("A RESET", "B BACK");
  present();
}

void renderIRAnalyzer() {
  beginFrame();
  drawAppHeader("IR Analyzer");
  frame.fillRoundRect(7, 68, W() - 14, 112, 9, PANEL);
  frame.setTextColor(WARN, PANEL);
  frame.setTextSize(1.15f);
  frame.setCursor(13, 82);
  frame.print("RMT next");
  frame.setTextSize(0.95f);
  frame.setTextColor(FG, PANEL);
  frame.setCursor(13, 111);
  frame.print("NEC + raw pulses");
  frame.setTextColor(MUTED, PANEL);
  frame.setCursor(13, 139);
  frame.print("Speaker amp will");
  frame.setCursor(13, 155);
  frame.print("disable during RX");
  drawFooter("A START", "B BACK");
  present();
}

void renderCurrent(bool action = false) {
  lastRender = millis();
  switch (current) {
    case AppId::Dashboard: renderDashboard(); break;
    case AppId::Stocks: renderPlaceholder("Stock Alerts", "Market adapter", "Live endpoint next"); break;
    case AppId::VoiceAI: renderPlaceholder("AI Push-to-Talk", "Mic + speaker", "STT / TTS next"); break;
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
  renderCurrent(false);
}

void handleButtons() {
  // StickS3 KEY1/front blue button -> M5.BtnA.
  // StickS3 KEY2/side button       -> M5.BtnB.
  if (!inApp) {
    if (M5.BtnA.wasClicked()) {
      selected = (selected + 1) % APP_COUNT;
      renderMenu();
    }
    if (M5.BtnB.wasClicked()) {
      enterSelected();
    }
    return;
  }

  if (current == AppId::WiFiMonitor && M5.BtnB.wasHold()) {
    startSetupPortal();
    return;
  }

  if (M5.BtnB.wasClicked()) {
    inApp = false;
    renderMenu();
    return;
  }

  if (M5.BtnA.wasClicked()) {
    if (current == AppId::WiFiMonitor && !wifiConnected()) {
      tryWiFiConnection();
    }
    if (current == AppId::TiltGame) {
      gameX = W() / 2.0f;
      gameY = H() / 2.0f;
      gameVx = gameVy = 0.0f;
    }
    renderCurrent(true);
  }
}

void periodicRefresh() {
  if (!inApp || setupPortalActive) return;

  const uint32_t now = millis();
  if (current == AppId::TiltGame && now - lastRender >= 40) {
    renderTiltGame();
    lastRender = now;
    return;
  }

  if (current == AppId::Level && now - lastRender >= 90) {
    renderLevel();
    lastRender = now;
    return;
  }

  if ((current == AppId::Clock || current == AppId::Dashboard || current == AppId::DeskPager ||
       current == AppId::Countdown || current == AppId::WiFiMonitor) && now - lastSecond >= 1000) {
    lastSecond = now;
    renderCurrent(false);
  }
}

void serviceSetupPortal() {
  if (!setupPortalActive) return;
  setupServer.handleClient();

  if (M5.BtnB.wasClicked() || millis() - setupStartedAt >= SETUP_TIMEOUT_MS) {
    stopSetupPortal();
    renderMenu();
  }
}

}  // namespace pn

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  // Portrait: rotating the physical device clockwise puts the blue/front A key at the bottom.
  M5.Display.setRotation(0);  // 135 x 240 portrait
  M5.Display.setTextWrap(false);

  pn::frame.setColorDepth(8);
  pn::frame.createSprite(M5.Display.width(), M5.Display.height());
  pn::frame.setTextWrap(false);

  pn::beginFrame();
  pn::drawStatusBar();
  pn::frame.setTextColor(pn::ACCENT, pn::BG);
  pn::frame.setTextSize(1.7f);
  pn::frame.setCursor(8, 70);
  pn::frame.print("Pocket Nexus");
  pn::frame.setTextSize(1.15f);
  pn::frame.setTextColor(pn::MUTED, pn::BG);
  pn::frame.setCursor(8, 103);
  pn::frame.printf("v%s", pn::VERSION);
  pn::frame.setCursor(8, 128);
  pn::frame.print("Portrait UI");
  pn::present();
  delay(350);

  pn::connectWiFi();
  if (!pn::setupPortalActive) {
    pn::renderMenu();
  }
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
