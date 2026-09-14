#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "driver/rmt.h"
#include "freertos/ringbuf.h"

#if __has_include("screensaver_rgb565.h")
#include "screensaver_rgb565.h"
#define PN_HAS_RGB_SAVER 1
#else
#define PN_HAS_RGB_SAVER 0
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define PN_WIFI_SSID ""
#define PN_WIFI_PASSWORD ""
#define PN_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"
#endif

namespace pn {

constexpr const char* VERSION = "0.5";
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
constexpr uint32_t WIFI_RETRY_MS = 12UL * 1000UL;
constexpr int IR_RECEIVE_PIN = 42;

static const char STOCK_URL[] = "https://raw.githubusercontent.com/XiminHu66/stock-alert/main/data/quotes.json";
static const char DIGICERT_G2_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

struct AppEntry { const char* menuName; const char* title; };
enum class AppId : uint8_t {
  Dashboard, Stocks, VoiceAI, DeskPager, Calendar, News, IRAnalyzer,
  Level, Clock, Countdown, TiltGame, Dice, WiFiMonitor, ScreenSaver, Count
};
constexpr AppEntry APPS[] = {
  {"首页", "随身中枢"}, {"股票行情", "股票行情"}, {"语音助手", "语音助手"},
  {"桌面信息", "桌面信息"}, {"日程", "日程"}, {"新闻", "新闻"},
  {"红外分析", "红外分析"}, {"水平仪", "水平仪"}, {"时钟", "网络时钟"},
  {"倒计时", "倒计时"}, {"倾斜游戏", "倾斜游戏"}, {"骰子", "骰子"},
  {"无线网络", "无线网络"}, {"屏保", "屏保"}
};
constexpr uint8_t APP_COUNT = static_cast<uint8_t>(AppId::Count);

M5Canvas frame(&M5.Display);
uint8_t selected = 0;
bool inApp = false;
AppId current = AppId::Dashboard;
uint8_t pageIndex = 0;
uint32_t lastRender = 0, lastSecond = 0, lastInteractionAt = 0;
bool screensaverActive = false;

WebServer setupServer(80);
bool setupRoutesInstalled = false, setupPortalActive = false;
uint32_t setupStartedAt = 0;
String setupApName, setupApPassword, setupToken;
String configuredSsid, configuredPassword;
uint32_t lastWifiAttempt = 0;
bool ntpConfigured = false;

int diceValue = 1;
float gameX = 67.0f, gameY = 132.0f, gameVx = 0.0f, gameVy = 0.0f;

struct StockQuote { const char* symbol; float price; float changePct; bool valid; };
StockQuote stocks[] = {
  {"QQQ",0,0,false},{"SPY",0,0,false},{"SMH",0,0,false},{"NVDA",0,0,false},
  {"AMD",0,0,false},{"TSM",0,0,false},{"MSFT",0,0,false},{"GOOGL",0,0,false}
};
constexpr uint8_t STOCK_COUNT = sizeof(stocks) / sizeof(stocks[0]);
String stockUpdated = "尚未刷新";
bool stockLoading = false;
String stockError;

RingbufHandle_t irRingBuffer = nullptr;
constexpr rmt_channel_t IR_RMT_CHANNEL = RMT_CHANNEL_0;
bool irActive = false, irHasFrame = false, irFrameValid = false, irRepeatFrame = false;
size_t irSymbolCount = 0;
uint32_t irRawData = 0;
uint16_t irAddress = 0;
uint8_t irCommand = 0;

int16_t* voiceBuffer = nullptr;
constexpr uint32_t VOICE_RATE = 16000;
constexpr size_t VOICE_SAMPLES = 16000;
String voiceStatus = "长按A录音并回放";

int W() { return frame.width(); }
int H() { return frame.height(); }

void fontSmall() { frame.setTextFont(&fonts::efontCN_12); frame.setTextSize(1); }
void fontUI() { frame.setTextFont(&fonts::efontCN_16_b); frame.setTextSize(1); }
void fontBig() { frame.setTextFont(&fonts::efontCN_16_b); frame.setTextSize(2); }

void beginFrame() {
  frame.fillSprite(BG);
  frame.setTextWrap(false);
  frame.setTextDatum(top_left);
  frame.setTextColor(FG, BG);
  fontSmall();
}
void present() { frame.pushSprite(0, 0); }

size_t utf8GlyphLen(uint8_t c) {
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

void drawWrappedText(const String& text, int x, int y, int maxWidth, int lineHeight,
                     int maxLines, uint16_t color = FG, uint16_t bg = BG) {
  fontSmall(); frame.setTextColor(color, bg);
  String line;
  int lines = 0;
  auto flush = [&]() {
    if (!line.length() || lines >= maxLines) return;
    frame.drawString(line, x, y + lines * lineHeight);
    line = ""; ++lines;
  };
  for (size_t i = 0; i < text.length() && lines < maxLines;) {
    if (text[i] == '\n') { flush(); ++i; continue; }
    size_t n = utf8GlyphLen((uint8_t)text[i]);
    if (i + n > text.length()) n = 1;
    String glyph = text.substring(i, i + n);
    i += n;
    String candidate = line + glyph;
    if (line.length() && frame.textWidth(candidate) > maxWidth) {
      flush();
      if (lines >= maxLines) break;
      line = glyph;
    } else line = candidate;
  }
  if (lines < maxLines) flush();
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool hasWiFiConfig() { return configuredSsid.length() > 0; }

void drawBatteryIcon(int x, int y, int level, bool charging) {
  const int w=24,h=12; frame.drawRoundRect(x,y,w,h,2,FG); frame.fillRect(x+w,y+4,2,4,FG);
  int pct=constrain(level,0,100), fillW=(w-4)*pct/100;
  uint16_t c=pct<=15?TFT_RED:(pct<=30?WARN:OK);
  if(fillW>0) frame.fillRoundRect(x+2,y+2,fillW,h-4,1,c);
  if(charging){ fontSmall(); frame.setTextColor(TFT_BLACK,c); frame.drawString("+",x+9,y-1); }
}

void drawStatusBar(const char* label="随身中枢") {
  frame.fillRect(0,0,W(),28,BLUE); fontSmall(); frame.setTextColor(FG,BLUE); frame.drawString(label,5,7);
  frame.fillCircle(75,14,4,wifiConnected()?OK:(setupPortalActive?WARN:MUTED));
  int bat=M5.Power.getBatteryLevel(); bool charging=static_cast<bool>(M5.Power.isCharging());
  drawBatteryIcon(84,8,bat<0?0:bat,charging); frame.setTextColor(FG,BLUE);
  frame.drawString(bat>=0?String(bat):String("--"),112,7);
}

void drawAppHeader(const char* title) {
  drawStatusBar(); fontUI(); frame.setTextColor(ACCENT,BG); frame.drawString(title,7,34); frame.drawFastHLine(7,58,W()-14,MUTED);
}

void drawFooter(const char* a="A 操作", const char* b="B 返回") {
  int y=H()-27; frame.fillRoundRect(5,y+2,W()-10,23,6,BLUE); fontSmall(); frame.setTextColor(FG,BLUE);
  frame.drawString(a,9,y+6); int bw=frame.textWidth(b); frame.drawString(b,W()-9-bw,y+6);
}

void drawPageTag(uint8_t page,uint8_t count){ if(count<=1)return; fontSmall(); frame.setTextColor(MUTED,BG); String p=String(page+1)+"/"+String(count); frame.drawString(p,W()-7-frame.textWidth(p),45); }

void drawInfoCard(int y,const char* label,const String& value,uint16_t valueColor=FG){
  frame.fillRoundRect(7,y,W()-14,44,8,PANEL); fontSmall(); frame.setTextColor(MUTED,PANEL); frame.drawString(label,13,y+5);
  fontUI(); frame.setTextColor(valueColor,PANEL);
  if(frame.textWidth(value)<=W()-28) frame.drawString(value,13,y+19); else { fontSmall(); drawWrappedText(value,13,y+20,W()-28,13,2,valueColor,PANEL); }
}

void drawBatteryCard(int y){
  int bat=M5.Power.getBatteryLevel(); int mv=M5.Power.getBatteryVoltage(); bool ch=static_cast<bool>(M5.Power.isCharging()); int pct=bat<0?0:constrain(bat,0,100);
  frame.fillRoundRect(7,y,W()-14,37,8,PANEL); fontSmall(); frame.setTextColor(MUTED,PANEL); frame.drawString(ch?"电量 · 充电中":"电量",13,y+4);
  if(mv>0) frame.drawString(String(mv/1000.0f,1)+"V",88,y+4); fontUI(); frame.setTextColor(pct<=20?WARN:FG,PANEL); frame.drawString(bat>=0?String(bat)+"%":"--",13,y+14);
  int bx=58,by=y+20,bw=60; frame.drawRoundRect(bx,by,bw,9,2,MUTED); int fw=(bw-4)*pct/100; uint16_t c=pct<=15?TFT_RED:(pct<=30?WARN:OK); if(fw>0)frame.fillRoundRect(bx+2,by+2,fw,5,1,c);
}

bool readTime(struct tm& t){ return getLocalTime(&t,10); }
String hhmm(){ struct tm t{}; if(!readTime(t))return "--:--"; char b[8]; strftime(b,sizeof(b),"%H:%M",&t); return String(b); }
String dateLine(){
  struct tm t{}; if(!readTime(t))return "时间尚未同步";
  static const char* wk[]={"周日","周一","周二","周三","周四","周五","周六"};
  return String(t.tm_mon+1)+"月"+String(t.tm_mday)+"日 "+wk[t.tm_wday];
}

String randomString(size_t length,const char* alphabet){ String o; size_t n=strlen(alphabet); o.reserve(length); for(size_t i=0;i<length;++i)o+=alphabet[esp_random()%n]; return o; }

void loadWiFiCredentials(){
  if(strlen(PN_WIFI_SSID)>0){configuredSsid=PN_WIFI_SSID;configuredPassword=PN_WIFI_PASSWORD;return;}
  Preferences p; if(p.begin("pocket-nexus",true)){configuredSsid=p.getString("ssid","");configuredPassword=p.getString("pass","");p.end();}
}
void saveWiFiCredentials(const String& ssid,const String& pass){ Preferences p; if(p.begin("pocket-nexus",false)){p.putString("ssid",ssid);p.putString("pass",pass);p.end();} }

void addSetupSecurityHeaders(){
  setupServer.sendHeader("Cache-Control","no-store, no-cache, must-revalidate"); setupServer.sendHeader("Pragma","no-cache");
  setupServer.sendHeader("X-Content-Type-Options","nosniff"); setupServer.sendHeader("X-Frame-Options","DENY"); setupServer.sendHeader("Referrer-Policy","no-referrer");
  setupServer.sendHeader("Content-Security-Policy","default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
}
String setupPageHtml(){
  String p; p.reserve(1800);
  p += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Pocket Nexus</title><style>body{font-family:system-ui;background:#0b0d10;color:#fff;padding:24px}main{max-width:520px;margin:auto;background:#151920;padding:24px;border-radius:18px}input,button{box-sizing:border-box;width:100%;font-size:17px;padding:13px;margin:8px 0;border-radius:10px}input{background:#0f1318;color:white;border:1px solid #39434f}button{border:0;background:#35d0ba;color:#07110f;font-weight:700}p{color:#aeb8c5;line-height:1.5}</style></head><body><main><h2>Pocket Nexus 无线网络</h2><p>此页面只存在于临时安全热点中，密码仅保存在设备本机。</p><form method='post' action='/save'><input type='hidden' name='token' value='");
  p+=setupToken; p+=F("'><input name='ssid' maxlength='32' placeholder='2.4 GHz Wi-Fi 名称' required><input name='password' type='password' maxlength='63' placeholder='Wi-Fi 密码'><button type='submit'>保存并重启</button></form><p>热点最多允许一台设备，并在 10 分钟后自动关闭。</p></main></body></html>"); return p;
}
void renderSetupPortal(){
  beginFrame(); drawStatusBar("安全配置"); fontUI(); frame.setTextColor(WARN,BG); frame.drawString("无线网络配置",7,35);
  fontSmall(); frame.setTextColor(MUTED,BG); frame.drawString("热点",8,67); frame.setTextColor(FG,BG); drawWrappedText(setupApName,8,82,W()-16,14,2);
  frame.setTextColor(MUTED,BG); frame.drawString("密码",8,112); frame.setTextColor(ACCENT,BG); drawWrappedText(setupApPassword,8,127,W()-16,14,2,ACCENT);
  frame.setTextColor(MUTED,BG); frame.drawString("浏览器打开 192.168.4.1",8,164); frame.drawString("1台设备 · 10分钟",8,185); drawFooter("A -","B 取消"); present();
}
void installSetupRoutes(){
  if(setupRoutesInstalled)return; setupRoutesInstalled=true;
  setupServer.on("/",HTTP_GET,[](){addSetupSecurityHeaders();setupServer.send(200,"text/html; charset=utf-8",setupPageHtml());});
  setupServer.on("/save",HTTP_POST,[](){addSetupSecurityHeaders();String token=setupServer.arg("token"),ssid=setupServer.arg("ssid"),pass=setupServer.arg("password");
    if(token!=setupToken){setupServer.send(403,"text/plain","Invalid token");return;} if(!ssid.length()||ssid.length()>32||pass.length()>63){setupServer.send(400,"text/plain","Invalid Wi-Fi");return;}
    saveWiFiCredentials(ssid,pass); setupServer.send(200,"text/html; charset=utf-8","<html><meta charset='utf-8'><body><h2>已保存</h2><p>设备正在重启。</p></body></html>"); delay(700);ESP.restart();});
  setupServer.onNotFound([](){addSetupSecurityHeaders();setupServer.sendHeader("Location","/",true);setupServer.send(302,"text/plain","");});
}
void stopSetupPortal(){ if(!setupPortalActive)return; setupServer.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);setupPortalActive=false;setupApPassword="";setupToken=""; }
void startSetupPortal(){
  if(setupPortalActive)return; static const char* safe="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";static const char* hex="0123456789abcdef";char suffix[5];snprintf(suffix,sizeof(suffix),"%04X",(uint16_t)(ESP.getEfuseMac()&0xFFFF));
  setupApName=String("PN-Setup-")+suffix;setupApPassword=randomString(12,safe);setupToken=randomString(24,hex);setupStartedAt=millis();WiFi.disconnect(true,false);delay(50);WiFi.mode(WIFI_AP);WiFi.softAP(setupApName.c_str(),setupApPassword.c_str(),1,false,1);setupPortalActive=true;installSetupRoutes();setupServer.begin();renderSetupPortal();
}

void beginWifiAttempt(){ if(!hasWiFiConfig()||setupPortalActive)return; lastWifiAttempt=millis();WiFi.mode(WIFI_STA);WiFi.begin(configuredSsid.c_str(),configuredPassword.c_str()); }
void serviceWiFi(){
  if(setupPortalActive||!hasWiFiConfig())return;
  if(wifiConnected()){
    if(!ntpConfigured){ configTzTime(PN_TIMEZONE,"pool.ntp.org","time.nist.gov"); ntpConfigured=true; }
    return;
  }
  if(millis()-lastWifiAttempt>=WIFI_RETRY_MS) beginWifiAttempt();
}
void connectWiFi(){
  loadWiFiCredentials(); WiFi.persistent(false); WiFi.setAutoReconnect(true); setenv("TZ",PN_TIMEZONE,1);tzset();
  if(!hasWiFiConfig()){startSetupPortal();return;} beginWifiAttempt();
  uint32_t s=millis();while(!wifiConnected()&&millis()-s<10000){M5.update();delay(50);} if(wifiConnected()){configTzTime(PN_TIMEZONE,"pool.ntp.org","time.nist.gov");ntpConfigured=true;}
}

bool fetchStocks(){
  if(!wifiConnected()){stockError="无线网络未连接";return false;} stockLoading=true;stockError="";
  WiFiClientSecure client; client.setCACert(DIGICERT_G2_CA); HTTPClient http; http.setTimeout(10000);
  if(!http.begin(client,STOCK_URL)){stockError="HTTPS 初始化失败";stockLoading=false;return false;}
  int code=http.GET(); if(code!=HTTP_CODE_OK){stockError=String("HTTP ")+code;http.end();stockLoading=false;return false;}
  String body=http.getString();http.end();DynamicJsonDocument doc(32768);DeserializationError e=deserializeJson(doc,body); if(e){stockError="行情 JSON 解析失败";stockLoading=false;return false;}
  JsonObject syms=doc["symbols"].as<JsonObject>();
  for(auto& q:stocks){JsonObject o=syms[q.symbol]; if(!o.isNull()){q.price=o["price"]|0.0f;q.changePct=o["changePct"]|0.0f;q.valid=true;}}
  stockUpdated=doc["generatedAt"]|"已更新";stockLoading=false;return true;
}

void renderMenu(){
  beginFrame();drawStatusBar();fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(String("应用 ")+String(selected+1)+"/"+String(APP_COUNT),7,33);
  constexpr uint8_t rows=4;uint8_t start=selected>=rows?selected-rows+1:0;
  for(uint8_t r=0;r<rows;++r){uint8_t idx=start+r;if(idx>=APP_COUNT)break;int y=48+r*39;bool active=idx==selected;frame.fillRoundRect(5,y,W()-10,34,7,active?BLUE:PANEL2);if(!active)frame.drawRoundRect(5,y,W()-10,34,7,MUTED);fontSmall();frame.setTextColor(active?FG:MUTED,active?BLUE:PANEL2);frame.drawString(String(idx+1),10,y+8);fontUI();frame.setTextColor(FG,active?BLUE:PANEL2);frame.drawString(APPS[idx].menuName,34,y+5);}
  drawFooter("A 下一项","B 打开");present();
}

void renderDashboard(){
  beginFrame();drawAppHeader("随身中枢");fontBig();frame.setTextColor(ACCENT,BG);frame.drawString(hhmm(),8,66);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(dateLine(),9,108);
  drawInfoCard(126,"无线网络",wifiConnected()?String(WiFi.RSSI())+" dBm":"未连接",wifiConnected()?OK:WARN);drawBatteryCard(174);drawFooter("A 刷新","B 返回");present();
}

void renderStocks(){
  beginFrame();drawAppHeader("股票行情");drawPageTag(pageIndex,2);uint8_t start=pageIndex*4;
  if(stockLoading){fontUI();frame.setTextColor(ACCENT,BG);frame.drawString("正在刷新行情…",8,85);} else {
    for(uint8_t i=0;i<4;++i){StockQuote& q=stocks[start+i];int y=68+i*33;frame.fillRoundRect(7,y,W()-14,29,6,PANEL);fontUI();frame.setTextColor(FG,PANEL);frame.drawString(q.symbol,12,y+5);fontSmall();
      if(q.valid){String p=String(q.price,2);frame.setTextColor(FG,PANEL);frame.drawString(p,52,y+7);String pct=String(q.changePct>=0?"+":"")+String(q.changePct,2)+"%";frame.setTextColor(q.changePct>=0?OK:TFT_RED,PANEL);frame.drawString(pct,W()-10-frame.textWidth(pct),y+7);}else{frame.setTextColor(MUTED,PANEL);frame.drawString("--",70,y+7);}}
    fontSmall();frame.setTextColor(stockError.length()?WARN:MUTED,BG);String s=stockError.length()?stockError:"长按A刷新真实行情";drawWrappedText(s,8,202,W()-16,13,1,stockError.length()?WARN:MUTED);
  }
  drawFooter("A 下一页","B 返回");present();
}

bool recordVoiceLoopback(){
  if(!voiceBuffer)voiceBuffer=(int16_t*)heap_caps_malloc(VOICE_SAMPLES*sizeof(int16_t),MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM); if(!voiceBuffer){voiceStatus="内存不足";return false;}
  voiceStatus="录音中…";beginFrame();drawAppHeader("语音助手");fontUI();frame.setTextColor(WARN,BG);drawWrappedText(voiceStatus,8,90,W()-16,20,3,WARN);drawFooter("A 录音","B 返回");present();
  M5.Speaker.end();M5.Mic.begin();constexpr size_t block=320;size_t pos=0;while(pos<VOICE_SAMPLES){size_t n=min(block,VOICE_SAMPLES-pos);if(M5.Mic.record(voiceBuffer+pos,n,VOICE_RATE))pos+=n;M5.update();delay(1);}while(M5.Mic.isRecording())delay(1);M5.Mic.end();
  voiceStatus="回放中…";beginFrame();drawAppHeader("语音助手");fontUI();frame.setTextColor(ACCENT,BG);drawWrappedText(voiceStatus,8,90,W()-16,20,3,ACCENT);present();M5.Speaker.begin();M5.Speaker.playRaw(voiceBuffer,VOICE_SAMPLES,VOICE_RATE,false,1,0);while(M5.Speaker.isPlaying()){M5.update();delay(1);}voiceStatus="麦克风/扬声器正常";return true;
}
void renderVoice(){
  beginFrame();drawAppHeader("语音助手");fontUI();frame.setTextColor(ACCENT,BG);drawWrappedText(voiceStatus,8,76,W()-16,21,4,ACCENT);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("长按A录制约1秒语音并立即回放。这样先验证内置麦克风与扬声器；AI联网处理将在安全后端接入后启用。",8,128,W()-16,15,5,FG);drawFooter("长按A 录音","B 返回");present();
}

void renderDesk(){beginFrame();drawAppHeader("桌面信息");fontBig();frame.setTextColor(ACCENT,BG);frame.drawString(hhmm(),8,70);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(dateLine(),8,113);drawInfoCard(135,"网络",wifiConnected()?"在线":"离线",wifiConnected()?OK:WARN);drawBatteryCard(180);drawFooter("A 刷新","B 返回");present();}
void renderCalendar(){beginFrame();drawAppHeader("日程");fontUI();frame.setTextColor(ACCENT,BG);frame.drawString(dateLine(),8,76);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("设备端日程功能已准备好，但私人 Google Calendar 需要经过受保护的后端适配，不能把账号令牌写进公开固件。",8,118,W()-16,15,6,FG);drawFooter("A 刷新","B 返回");present();}
void renderNews(){beginFrame();drawAppHeader("新闻");fontUI();frame.setTextColor(ACCENT,BG);frame.drawString("RSS 精简源",8,76);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("现有 RSS 数据源约 450KB，不适合 StickS3 直接下载。下一步会由 PT Universe 后端输出精简新闻流，再在这里分页显示。",8,116,W()-16,15,6,FG);drawFooter("A 刷新","B 返回");present();}

void readAccel(float& ax,float& ay,float& az){ax=ay=0;az=1;if(M5.Imu.update()){auto d=M5.Imu.getImuData();ax=d.accel.x;ay=d.accel.y;az=d.accel.z;}}
void renderLevel(){float ax,ay,az;readAccel(ax,ay,az);float roll=atan2f(ay,az)*180/PI,pitch=atan2f(-ax,sqrtf(ay*ay+az*az))*180/PI;beginFrame();drawAppHeader("水平仪");fontSmall();frame.setTextColor(FG,BG);frame.drawString(String("俯仰 ")+String(pitch,1)+"°",8,70);frame.drawString(String("横滚 ")+String(roll,1)+"°",8,89);int cx=W()/2,cy=151;frame.drawCircle(cx,cy,42,MUTED);frame.drawCircle(cx,cy,21,MUTED);frame.drawFastHLine(cx-38,cy,76,MUTED);frame.drawFastVLine(cx,cy-38,76,MUTED);int bx=constrain(cx+(int)(ay*37),cx-37,cx+37),by=constrain(cy-(int)(ax*37),cy-37,cy+37);frame.fillCircle(bx,by,7,ACCENT);drawFooter("A 刷新","B 返回");present();}
void renderClock(){beginFrame();drawAppHeader("网络时钟");fontBig();frame.setTextColor(ACCENT,BG);frame.drawString(hhmm(),8,80);fontUI();frame.setTextColor(FG,BG);frame.drawString(dateLine(),8,126);fontSmall();frame.setTextColor(wifiConnected()?OK:WARN,BG);frame.drawString(wifiConnected()?"网络时间已同步":"离线，继续使用本机时间",8,158);drawBatteryCard(176);drawFooter("A 刷新","B 返回");present();}
void renderCountdown(){beginFrame();drawAppHeader("倒计时");fontUI();frame.setTextColor(WARN,BG);frame.drawString("尚未设置事件",8,88);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("倒计时会和日程共用同一个安全事件源。接入后将自动显示最近事件的天/小时倒数。",8,130,W()-16,15,5,FG);drawFooter("A 刷新","B 返回");present();}
void renderDice(bool roll){if(roll){diceValue=1+(int)(esp_random()%6);M5.Speaker.tone(2200,40);}beginFrame();drawAppHeader("骰子");frame.fillRoundRect(18,78,W()-36,108,16,PANEL);fontBig();frame.setTextSize(3);frame.setTextDatum(middle_center);frame.setTextColor(ACCENT,PANEL);frame.drawString(String(diceValue),W()/2,132);frame.setTextDatum(top_left);drawFooter("A 掷骰子","B 返回");present();}
void renderGame(){float ax,ay,az;readAccel(ax,ay,az);gameVx=constrain(gameVx+ay*.28f,-3.5f,3.5f);gameVy=constrain(gameVy-ax*.28f,-3.5f,3.5f);gameVx*=.96f;gameVy*=.96f;gameX=constrain(gameX+gameVx,14.0f,(float)W()-14);gameY=constrain(gameY+gameVy,76.0f,(float)H()-45);beginFrame();drawAppHeader("倾斜游戏");frame.drawRoundRect(7,66,W()-14,H()-109,8,MUTED);frame.fillCircle((int)gameX,(int)gameY,7,ACCENT);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString("倾斜设备移动小球",8,194);drawFooter("A 重置","B 返回");present();}

void renderWiFi(){
  beginFrame();drawAppHeader("无线网络");drawPageTag(pageIndex,2);
  if(pageIndex==0){drawInfoCard(68,"状态",wifiConnected()?"已连接":(hasWiFiConfig()?"正在重连":"未配置"),wifiConnected()?OK:WARN);drawInfoCard(116,"网络名称",wifiConnected()?WiFi.SSID():(hasWiFiConfig()?configuredSsid:"无"));drawInfoCard(164,"信号",wifiConnected()?String(WiFi.RSSI())+" dBm":"--");}
  else{frame.fillRoundRect(7,68,W()-14,123,8,PANEL);fontSmall();frame.setTextColor(FG,PANEL);String b=wifiConnected()?String("IP: ")+WiFi.localIP().toString()+"\n信道: "+String(WiFi.channel())+"\n信号: "+String(WiFi.RSSI())+" dBm\n\n设备会自动重连。\n长按B重新配置。":String("已保存网络: ")+(hasWiFiConfig()?configuredSsid:"无")+"\n\n每12秒自动尝试重连。\n长按B进入安全配置。";drawWrappedText(b,13,82,W()-26,15,8,FG,PANEL);}
  drawFooter("A 下一页","B 返回");present();
}

bool decodeNEC(const rmt_item32_t* s,size_t count,uint32_t* raw,bool* repeat){*raw=0;*repeat=false;if(count<2)return false;uint32_t h0=s[0].duration0,h1=s[0].duration1;if(h0>8000&&h1>4000){if(count<33)return false;}else if(h0>8000&&h1>2000&&h1<3000){*repeat=true;return false;}else return false;for(int i=0;i<32;++i){uint32_t m=s[i+1].duration0,sp=s[i+1].duration1;if(m<300||m>800)return false;if(sp>1000)*raw|=(1UL<<i);}uint8_t c=(*raw>>16)&0xFF,iv=(*raw>>24)&0xFF;return(c^iv)==0xFF;}
bool startIr(){if(irActive)return true;M5.Speaker.end();M5.Power.setExtOutput(true,m5::ext_none);rmt_config_t c={};c.rmt_mode=RMT_MODE_RX;c.channel=IR_RMT_CHANNEL;c.gpio_num=(gpio_num_t)IR_RECEIVE_PIN;c.clk_div=80;c.mem_block_num=2;c.rx_config.filter_en=true;c.rx_config.filter_ticks_thresh=80;c.rx_config.idle_threshold=15000;if(rmt_config(&c)!=ESP_OK||rmt_driver_install(IR_RMT_CHANNEL,2048,0)!=ESP_OK||rmt_get_ringbuf_handle(IR_RMT_CHANNEL,&irRingBuffer)!=ESP_OK||!irRingBuffer||rmt_rx_start(IR_RMT_CHANNEL,true)!=ESP_OK){rmt_driver_uninstall(IR_RMT_CHANNEL);irRingBuffer=nullptr;M5.Power.setExtOutput(false,m5::ext_none);M5.Speaker.begin();return false;}irActive=true;irHasFrame=irFrameValid=irRepeatFrame=false;return true;}
void stopIr(){if(!irActive)return;rmt_rx_stop(IR_RMT_CHANNEL);rmt_driver_uninstall(IR_RMT_CHANNEL);irRingBuffer=nullptr;M5.Power.setExtOutput(false,m5::ext_none);M5.Speaker.begin();irActive=false;}
bool pollIr(){if(!irActive||!irRingBuffer)return false;size_t sz=0;auto* items=(rmt_item32_t*)xRingbufferReceive(irRingBuffer,&sz,0);if(!items)return false;size_t count=sz/sizeof(rmt_item32_t);uint32_t raw=0;bool rep=false;bool valid=decodeNEC(items,count,&raw,&rep);irHasFrame=true;irFrameValid=valid;irRepeatFrame=rep;irSymbolCount=count;irRawData=raw;irAddress=raw&0xFFFF;irCommand=(raw>>16)&0xFF;vRingbufferReturnItem(irRingBuffer,(void*)items);return true;}
void renderIR(){beginFrame();drawAppHeader("红外分析");drawPageTag(pageIndex,2);if(!irActive){fontSmall();frame.setTextColor(WARN,BG);drawWrappedText("红外接收器未能启动，可以稍后再测试。",8,78,W()-16,15,6,WARN);}else if(pageIndex==0){if(!irHasFrame){fontUI();frame.setTextColor(ACCENT,BG);drawWrappedText("将遥控器对准设备",8,76,W()-16,20,3,ACCENT);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("按下任意遥控按键，建议距离至少约30厘米。",8,135,W()-16,15,5,FG);}else if(irFrameValid){drawInfoCard(72,"协议","NEC",OK);drawInfoCard(120,"地址",String("0x")+String(irAddress,HEX));drawInfoCard(168,"指令",String("0x")+String(irCommand,HEX));}else{fontUI();frame.setTextColor(WARN,BG);frame.drawString(irRepeatFrame?"NEC 重复码":"收到原始信号",8,80);fontSmall();drawWrappedText(String("捕获 ")+String(irSymbolCount)+" 个 RMT 符号。下一页查看详情。",8,125,W()-16,15,5,FG);}}else{frame.fillRoundRect(7,70,W()-14,120,8,PANEL);fontSmall();frame.setTextColor(FG,PANEL);String b=!irHasFrame?"正在等待红外信号。接收期间扬声器功放会关闭。":(irFrameValid?String("协议: NEC\n地址: 0x")+String(irAddress,HEX)+"\n指令: 0x"+String(irCommand,HEX)+"\n原始码: 0x"+String(irRawData,HEX)+"\n符号: "+String(irSymbolCount):String("已捕获信号，但未通过 NEC 校验。\n符号: ")+String(irSymbolCount));drawWrappedText(b,13,82,W()-26,15,8,FG,PANEL);}drawFooter("A 下一页","B 返回");present();}

void renderDefaultSaver(){M5.Display.fillScreen(TFT_BLACK);M5.Display.setTextColor(TFT_WHITE);M5.Display.setTextDatum(middle_center);M5.Display.setTextFont(&fonts::efontCN_16_b);M5.Display.setTextSize(2);M5.Display.drawString(hhmm(),67,92);M5.Display.setTextSize(1);M5.Display.drawString(dateLine(),67,135);M5.Display.setTextDatum(top_left);}
void renderCustomSaver(){
#if PN_HAS_RGB_SAVER
  M5.Display.pushImage(0,0,PN_SCREENSAVER_WIDTH,PN_SCREENSAVER_HEIGHT,PN_SCREENSAVER_PIXELS);
#else
  renderDefaultSaver();
#endif
}
void activateSaver(){if(setupPortalActive||irActive)return;screensaverActive=true;renderCustomSaver();}
void leaveSaver(){if(!screensaverActive)return;screensaverActive=false;lastInteractionAt=millis();renderMenu();}
void renderSaver(){beginFrame();drawAppHeader("屏保");fontUI();frame.setTextColor(ACCENT,BG);frame.drawString(PN_HAS_RGB_SAVER?"自定义图片已载入":"默认时钟屏保",8,82);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("设备无操作60秒后自动进入屏保。长按A立即预览，任意按键退出。",8,128,W()-16,15,5,FG);drawFooter("长按A 预览","B 返回");present();}

bool paged(AppId a){return a==AppId::Stocks||a==AppId::WiFiMonitor||a==AppId::IRAnalyzer;}
uint8_t pageCount(AppId a){return paged(a)?2:1;}

void renderCurrent(bool action=false){lastRender=millis();switch(current){case AppId::Dashboard:renderDashboard();break;case AppId::Stocks:renderStocks();break;case AppId::VoiceAI:renderVoice();break;case AppId::DeskPager:renderDesk();break;case AppId::Calendar:renderCalendar();break;case AppId::News:renderNews();break;case AppId::IRAnalyzer:renderIR();break;case AppId::Level:renderLevel();break;case AppId::Clock:renderClock();break;case AppId::Countdown:renderCountdown();break;case AppId::TiltGame:renderGame();break;case AppId::Dice:renderDice(action);break;case AppId::WiFiMonitor:renderWiFi();break;case AppId::ScreenSaver:renderSaver();break;default:break;}}
void enterSelected(){current=(AppId)selected;inApp=true;pageIndex=0;if(current==AppId::TiltGame){gameX=W()/2.0f;gameY=H()/2.0f;gameVx=gameVy=0;}if(current==AppId::IRAnalyzer)startIr();if(current==AppId::Stocks&&!stocks[0].valid&&wifiConnected())fetchStocks();renderCurrent(false);}
void leaveApp(){if(current==AppId::IRAnalyzer)stopIr();inApp=false;pageIndex=0;renderMenu();}
void performAction(){if(current==AppId::Stocks){fetchStocks();}else if(current==AppId::VoiceAI){recordVoiceLoopback();}else if(current==AppId::ScreenSaver){activateSaver();return;}else if(current==AppId::TiltGame){gameX=W()/2.0f;gameY=H()/2.0f;gameVx=gameVy=0;}else if(current==AppId::IRAnalyzer){irHasFrame=irFrameValid=irRepeatFrame=false;}else if(current==AppId::WiFiMonitor){beginWifiAttempt();}renderCurrent(true);}
void handleButtons(){bool ac=M5.BtnA.wasClicked(),ah=M5.BtnA.wasHold(),bc=M5.BtnB.wasClicked(),bh=M5.BtnB.wasHold();if(screensaverActive){if(ac||ah||bc||bh)leaveSaver();return;}if(ac||ah||bc||bh)lastInteractionAt=millis();if(!inApp){if(ac){selected=(selected+1)%APP_COUNT;renderMenu();}if(bc)enterSelected();return;}if(current==AppId::WiFiMonitor&&bh){startSetupPortal();return;}if(bc){leaveApp();return;}if(ah){performAction();return;}if(ac){if(paged(current)){pageIndex=(pageIndex+1)%pageCount(current);renderCurrent(false);}else performAction();}}

void periodicRefresh(){if(setupPortalActive)return;uint32_t now=millis();if(!screensaverActive&&!irActive&&now-lastInteractionAt>=SCREENSAVER_TIMEOUT_MS){activateSaver();return;}if(screensaverActive)return;if(!inApp)return;if(current==AppId::IRAnalyzer){if(pollIr())renderIR();return;}if(current==AppId::TiltGame&&now-lastRender>=40){renderGame();return;}if(current==AppId::Level&&now-lastRender>=90){renderLevel();return;}if((current==AppId::Clock||current==AppId::Dashboard||current==AppId::DeskPager||current==AppId::WiFiMonitor)&&now-lastSecond>=1000){lastSecond=now;renderCurrent(false);}}
void serviceSetupPortal(){if(!setupPortalActive)return;setupServer.handleClient();if(M5.BtnB.wasClicked()||millis()-setupStartedAt>=SETUP_TIMEOUT_MS){stopSetupPortal();lastInteractionAt=millis();renderMenu();}}

} // namespace pn

void setup(){auto cfg=M5.config();M5.begin(cfg);Serial.begin(115200);M5.Display.setRotation(0);M5.Display.setTextWrap(false);pn::frame.setColorDepth(16);pn::frame.createSprite(M5.Display.width(),M5.Display.height());pn::beginFrame();pn::drawStatusBar();pn::fontUI();pn::frame.setTextColor(pn::ACCENT,pn::BG);pn::frame.drawString("Pocket Nexus",8,78);pn::fontSmall();pn::frame.setTextColor(pn::MUTED,pn::BG);pn::frame.drawString(String("版本 ")+pn::VERSION,8,115);pn::frame.drawString("中文界面 · 自动重连",8,140);pn::present();delay(350);pn::lastInteractionAt=millis();pn::connectWiFi();if(!pn::setupPortalActive)pn::renderMenu();}
void loop(){M5.update();pn::serviceWiFi();pn::serviceSetupPortal();if(!pn::setupPortalActive){pn::handleButtons();pn::periodicRefresh();}delay(5);}
