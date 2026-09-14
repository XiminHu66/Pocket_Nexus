#include <Arduino.h>
#include <M5Unified.h>
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

constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t FG = TFT_WHITE;
constexpr uint16_t MUTED = TFT_DARKGREY;
constexpr uint16_t ACCENT = TFT_CYAN;
constexpr uint16_t OK = TFT_GREEN;
constexpr uint16_t WARN = TFT_YELLOW;
constexpr const char* SETUP_AP_NAME = "PocketNexus-Setup";

struct AppEntry {
  const char* shortName;
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
    {"Nexus", "Pocket Nexus"},
    {"Stocks", "Stock Alerts"},
    {"Voice", "AI Push-to-Talk"},
    {"Pager", "Desk Pager"},
    {"Cal", "Next Calendar"},
    {"News", "RSS / News"},
    {"IR", "IR Analyzer"},
    {"Level", "Level / Attitude"},
    {"Clock", "NTP Clock"},
    {"Count", "Countdown"},
    {"Game", "Tilt Game"},
    {"Dice", "Dice / Random"},
    {"WiFi", "Wi-Fi Monitor"},
};

constexpr uint8_t APP_COUNT = static_cast<uint8_t>(AppId::Count);
uint8_t selected = 0;
bool inApp = false;
AppId current = AppId::Dashboard;
uint32_t lastRender = 0;
uint32_t lastSecond = 0;
int diceValue = 1;
float gameX = 120.0f;
float gameY = 68.0f;
float gameVx = 0.0f;
float gameVy = 0.0f;

WebServer setupServer(80);
bool setupPortalActive = false;
String configuredSsid;
String configuredPassword;

void clearScreen() {
  M5.Display.fillScreen(BG);
  M5.Display.setTextColor(FG, BG);
  M5.Display.setTextSize(1);
}

void drawHeader(const char* title) {
  M5.Display.fillRect(0, 0, 240, 22, TFT_DARKGREY);
  M5.Display.setTextColor(FG, TFT_DARKGREY);
  M5.Display.setCursor(6, 6);
  M5.Display.print(title);
  M5.Display.setTextColor(FG, BG);
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

void drawFooter(const char* left = "A: next/action", const char* right = "B: back/select") {
  M5.Display.drawFastHLine(0, 116, 240, TFT_DARKGREY);
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(4, 121);
  M5.Display.print(left);
  M5.Display.setCursor(132, 121);
  M5.Display.print(right);
  M5.Display.setTextColor(FG, BG);
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

String setupPageHtml() {
  String page;
  page.reserve(1800);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Pocket Nexus Setup</title><style>");
  page += F("body{font-family:system-ui;background:#0b0d10;color:#f5f7fa;margin:0;padding:24px}");
  page += F("main{max-width:520px;margin:auto;background:#151920;padding:24px;border-radius:18px}");
  page += F("input,button{box-sizing:border-box;width:100%;font-size:17px;padding:13px;margin:8px 0;border-radius:10px}");
  page += F("input{background:#0f1318;color:white;border:1px solid #39434f}button{border:0;background:#35d0ba;color:#07110f;font-weight:700}");
  page += F("p{color:#aeb8c5;line-height:1.5}</style></head><body><main>");
  page += F("<h2>Pocket Nexus Wi-Fi</h2><p>Enter your 2.4 GHz Wi-Fi credentials. They are stored only on this StickS3.</p>");
  page += F("<form method='post' action='/save'><input name='ssid' maxlength='32' placeholder='Wi-Fi SSID' required>");
  page += F("<input name='password' type='password' maxlength='64' placeholder='Wi-Fi password'>");
  page += F("<button type='submit'>Save & Restart</button></form>");
  page += F("<p>Setup hotspot: PocketNexus-Setup<br>Address: 192.168.4.1</p></main></body></html>");
  return page;
}

void startSetupPortal() {
  if (setupPortalActive) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_NAME);
  setupPortalActive = true;

  setupServer.on("/", HTTP_GET, []() {
    setupServer.send(200, "text/html; charset=utf-8", setupPageHtml());
  });

  setupServer.on("/save", HTTP_POST, []() {
    const String ssid = setupServer.arg("ssid");
    const String password = setupServer.arg("password");
    if (ssid.length() == 0) {
      setupServer.send(400, "text/plain", "SSID is required");
      return;
    }
    saveWiFiCredentials(ssid, password);
    setupServer.send(200, "text/html; charset=utf-8",
                     "<html><body style='font-family:system-ui'><h2>Saved</h2><p>Pocket Nexus is restarting...</p></body></html>");
    delay(700);
    ESP.restart();
  });

  setupServer.onNotFound([]() {
    setupServer.sendHeader("Location", "/", true);
    setupServer.send(302, "text/plain", "");
  });

  setupServer.begin();

  clearScreen();
  drawHeader("Wi-Fi Setup");
  M5.Display.setTextColor(WARN, BG);
  M5.Display.setCursor(8, 34);
  M5.Display.print("Connect phone to:");
  M5.Display.setTextColor(ACCENT, BG);
  M5.Display.setCursor(8, 52);
  M5.Display.print(SETUP_AP_NAME);
  M5.Display.setTextColor(FG, BG);
  M5.Display.setCursor(8, 74);
  M5.Display.print("Open 192.168.4.1");
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(8, 96);
  M5.Display.print("Then save Wi-Fi + restart");
  delay(1200);
}

bool tryWiFiConnection() {
  if (!hasWiFiConfig()) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());

  clearScreen();
  drawHeader("Pocket Nexus");
  M5.Display.setCursor(8, 40);
  M5.Display.print("Connecting Wi-Fi");

  const uint32_t started = millis();
  while (!wifiConnected() && millis() - started < 8000) {
    M5.Display.print('.');
    delay(250);
  }

  if (!wifiConnected()) return false;

  configTzTime(PN_TIMEZONE, "pool.ntp.org", "time.nist.gov");
  return true;
}

void connectWiFi() {
  loadWiFiCredentials();
  if (!tryWiFiConnection()) {
    startSetupPortal();
  }
}

void renderMenu() {
  clearScreen();
  drawHeader("Pocket Nexus  v0.1");

  const uint8_t rows = 5;
  uint8_t start = 0;
  if (selected >= rows) start = selected - rows + 1;

  for (uint8_t row = 0; row < rows; ++row) {
    const uint8_t idx = start + row;
    if (idx >= APP_COUNT) break;
    const int y = 27 + row * 17;
    if (idx == selected) {
      M5.Display.fillRoundRect(4, y - 2, 232, 16, 3, ACCENT);
      M5.Display.setTextColor(TFT_BLACK, ACCENT);
    } else {
      M5.Display.setTextColor(FG, BG);
    }
    M5.Display.setCursor(10, y);
    M5.Display.printf("%02u  %s", idx + 1, APPS[idx].title);
  }

  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(5, 121);
  M5.Display.print("A next");
  M5.Display.setCursor(174, 121);
  M5.Display.print("B open");
}

void renderDashboard() {
  clearScreen();
  drawHeader("Pocket Nexus");

  M5.Display.setTextColor(ACCENT, BG);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(8, 31);
  M5.Display.print(hhmm());
  M5.Display.setTextSize(1);

  M5.Display.setTextColor(FG, BG);
  M5.Display.setCursor(10, 66);
  M5.Display.print(dateLine());
  M5.Display.setCursor(10, 84);
  if (wifiConnected()) {
    M5.Display.printf("Wi-Fi  %d dBm", WiFi.RSSI());
  } else if (setupPortalActive) {
    M5.Display.print("Wi-Fi setup AP active");
  } else {
    M5.Display.print("Wi-Fi  offline");
  }
  M5.Display.setCursor(10, 100);
  M5.Display.print("13 modules loaded");
  drawFooter("A: refresh", "B: menu");
}

void renderPlaceholder(const char* title, const char* line1, const char* line2) {
  clearScreen();
  drawHeader(title);
  M5.Display.setCursor(10, 42);
  M5.Display.print(line1);
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(10, 62);
  M5.Display.print(line2);
  M5.Display.setCursor(10, 90);
  M5.Display.print(strlen(PN_API_BASE_URL) ? "API configured" : "API not configured");
  drawFooter("A: refresh", "B: menu");
}

void renderDeskPager() {
  clearScreen();
  drawHeader("Desk Pager");
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(ACCENT, BG);
  M5.Display.setCursor(8, 30);
  M5.Display.print(hhmm());
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(FG, BG);
  M5.Display.setCursor(10, 62);
  M5.Display.print(dateLine());
  M5.Display.setCursor(10, 82);
  M5.Display.printf("Wi-Fi: %s", wifiConnected() ? "online" : "offline");
  if (wifiConnected()) {
    M5.Display.setCursor(10, 98);
    M5.Display.printf("Signal: %d dBm", WiFi.RSSI());
  }
  drawFooter("A: refresh", "B: menu");
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

  clearScreen();
  drawHeader("Level / Attitude");
  M5.Display.setCursor(8, 32);
  M5.Display.printf("Pitch %6.1f deg", pitch);
  M5.Display.setCursor(8, 48);
  M5.Display.printf("Roll  %6.1f deg", roll);

  const int cx = 180;
  const int cy = 72;
  M5.Display.drawCircle(cx, cy, 32, FG);
  M5.Display.drawFastHLine(cx - 25, cy, 50, MUTED);
  M5.Display.drawFastVLine(cx, cy - 25, 50, MUTED);
  const int bx = constrain(cx + static_cast<int>(roll * 1.2f), cx - 27, cx + 27);
  const int by = constrain(cy + static_cast<int>(pitch * 1.2f), cy - 27, cy + 27);
  M5.Display.fillCircle(bx, by, 5, ACCENT);
  drawFooter("A: zero later", "B: menu");
}

void renderClock() {
  clearScreen();
  drawHeader("NTP Clock");
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(ACCENT, BG);
  M5.Display.setCursor(12, 40);
  M5.Display.print(hhmm(true));
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(FG, BG);
  M5.Display.setCursor(12, 80);
  M5.Display.print(dateLine());
  M5.Display.setCursor(12, 98);
  M5.Display.print(wifiConnected() ? "NTP / online" : "offline / RTC only");
  drawFooter("A: refresh", "B: menu");
}

void renderCountdown() {
  clearScreen();
  drawHeader("Countdown");
  M5.Display.setCursor(10, 33);
  M5.Display.setTextColor(ACCENT, BG);
  M5.Display.print(PN_COUNTDOWN_LABEL);
  M5.Display.setTextColor(FG, BG);

  if (PN_COUNTDOWN_EPOCH <= 0) {
    M5.Display.setCursor(10, 60);
    M5.Display.print("Not configured");
    M5.Display.setTextColor(MUTED, BG);
    M5.Display.setCursor(10, 78);
    M5.Display.print("Remote config planned");
    M5.Display.setCursor(10, 92);
    M5.Display.print("for network builds");
  } else {
    const time_t now = time(nullptr);
    long long delta = static_cast<long long>(PN_COUNTDOWN_EPOCH) - static_cast<long long>(now);
    const bool overdue = delta < 0;
    if (overdue) delta = -delta;
    const long long days = delta / 86400LL;
    const int hours = (delta % 86400LL) / 3600LL;
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 57);
    M5.Display.printf("%lldd %02dh", days, hours);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 88);
    M5.Display.print(overdue ? "overdue" : "remaining");
  }
  drawFooter("A: refresh", "B: menu");
}

void renderDice(bool rollNow) {
  if (rollNow) {
    diceValue = 1 + static_cast<int>(esp_random() % 6);
    M5.Speaker.tone(2400, 45);
  }

  clearScreen();
  drawHeader("Dice / Random");
  M5.Display.setTextColor(ACCENT, BG);
  M5.Display.setTextSize(6);
  M5.Display.setCursor(96, 40);
  M5.Display.print(diceValue);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(FG, BG);
  drawFooter("A: roll", "B: menu");
}

void renderWiFiMonitor() {
  clearScreen();
  drawHeader("Wi-Fi Monitor");
  M5.Display.setCursor(8, 32);

  if (wifiConnected()) {
    M5.Display.setTextColor(OK, BG);
    M5.Display.print("Connected");
    M5.Display.setTextColor(FG, BG);
    M5.Display.setCursor(8, 50);
    M5.Display.printf("SSID: %s", WiFi.SSID().c_str());
    M5.Display.setCursor(8, 66);
    M5.Display.printf("RSSI: %d dBm", WiFi.RSSI());
    M5.Display.setCursor(8, 82);
    M5.Display.printf("IP: %s", WiFi.localIP().toString().c_str());
    M5.Display.setCursor(8, 98);
    M5.Display.printf("Ch: %d", WiFi.channel());
  } else if (setupPortalActive) {
    M5.Display.setTextColor(WARN, BG);
    M5.Display.print("Setup portal active");
    M5.Display.setTextColor(FG, BG);
    M5.Display.setCursor(8, 52);
    M5.Display.print(SETUP_AP_NAME);
    M5.Display.setCursor(8, 70);
    M5.Display.print("Open: 192.168.4.1");
    M5.Display.setTextColor(MUTED, BG);
    M5.Display.setCursor(8, 90);
    M5.Display.print("Credentials stay on-device");
  } else if (hasWiFiConfig()) {
    M5.Display.setTextColor(WARN, BG);
    M5.Display.print("Disconnected");
    M5.Display.setCursor(8, 52);
    M5.Display.print(configuredSsid);
  } else {
    M5.Display.setTextColor(WARN, BG);
    M5.Display.print("No Wi-Fi credentials");
  }
  drawFooter("A: reconnect", "B: menu");
}

void renderTiltGame() {
  float ax, ay, az;
  readAccel(ax, ay, az);
  gameVx = constrain(gameVx + ax * 0.25f, -3.5f, 3.5f);
  gameVy = constrain(gameVy + ay * 0.25f, -3.5f, 3.5f);
  gameVx *= 0.96f;
  gameVy *= 0.96f;
  gameX = constrain(gameX + gameVx, 7.0f, 233.0f);
  gameY = constrain(gameY + gameVy, 29.0f, 109.0f);

  clearScreen();
  drawHeader("Tilt Game");
  M5.Display.drawRect(4, 25, 232, 88, MUTED);
  M5.Display.fillCircle(static_cast<int>(gameX), static_cast<int>(gameY), 6, ACCENT);
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(8, 118);
  M5.Display.print("Tilt to move   B: menu");
}

void renderIRAnalyzer() {
  clearScreen();
  drawHeader("IR Analyzer");
  M5.Display.setTextColor(WARN, BG);
  M5.Display.setCursor(8, 34);
  M5.Display.print("RMT receiver scaffolded");
  M5.Display.setTextColor(FG, BG);
  M5.Display.setCursor(8, 54);
  M5.Display.print("Target: NEC + raw pulses");
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(8, 74);
  M5.Display.print("Speaker must be disabled");
  M5.Display.setCursor(8, 90);
  M5.Display.print("while IR RX is active.");
  drawFooter("A: v0.2", "B: menu");
}

void renderCurrent(bool action = false) {
  lastRender = millis();
  switch (current) {
    case AppId::Dashboard: renderDashboard(); break;
    case AppId::Stocks: renderPlaceholder("Stock Alerts", "Network module ready", "Endpoint wiring: v0.2"); break;
    case AppId::VoiceAI: renderPlaceholder("AI Push-to-Talk", "Mic / speaker available", "Upload + STT/TTS: v0.2"); break;
    case AppId::DeskPager: renderDeskPager(); break;
    case AppId::Calendar: renderPlaceholder("Next Calendar", "Calendar adapter ready", "Endpoint wiring: v0.2"); break;
    case AppId::News: renderPlaceholder("RSS / News", "News adapter ready", "Endpoint wiring: v0.2"); break;
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
  renderCurrent(false);
}

void handleButtons() {
  if (!inApp) {
    if (M5.BtnA.wasPressed()) {
      selected = (selected + 1) % APP_COUNT;
      renderMenu();
    }
    if (M5.BtnB.wasPressed()) {
      enterSelected();
    }
    return;
  }

  if (M5.BtnB.wasPressed()) {
    inApp = false;
    renderMenu();
    return;
  }

  if (M5.BtnA.wasPressed()) {
    if (current == AppId::WiFiMonitor) {
      if (hasWiFiConfig() && !setupPortalActive) {
        WiFi.disconnect();
        delay(50);
        WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());
      } else if (!wifiConnected()) {
        startSetupPortal();
      }
    }
    renderCurrent(true);
  }
}

void periodicRefresh() {
  if (!inApp) return;

  const uint32_t now = millis();
  if (current == AppId::TiltGame && now - lastRender >= 35) {
    renderTiltGame();
    lastRender = now;
    return;
  }

  if ((current == AppId::Level) && now - lastRender >= 100) {
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
  if (setupPortalActive) setupServer.handleClient();
}

}  // namespace pn

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  M5.Display.setRotation(1);  // 240 x 135 landscape
  M5.Display.setTextFont(1);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);

  pn::clearScreen();
  pn::drawHeader("Pocket Nexus");
  M5.Display.setCursor(8, 42);
  M5.Display.print("Booting v0.1...");
  delay(250);

  pn::connectWiFi();
  pn::renderMenu();
}

void loop() {
  M5.update();
  pn::serviceSetupPortal();
  pn::handleButtons();
  pn::periodicRefresh();
  delay(5);
}
