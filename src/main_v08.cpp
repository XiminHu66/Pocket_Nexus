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

constexpr const char* VERSION = "0.8";
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
static const char POCKET_API[] = "https://pocket-nexus-api.summer07-nanjolno.workers.dev";

static const char GTS_ROOT_R4[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIICCjCCAZGgAwIBAgIQbkepyIuUtui7OyrYorLBmTAKBggqhkjOPQQDAzBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQA
IgNiAATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzu
hXyiQHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/l
xKvRHYqjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1Ud
DgQWBBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNnADBkAjBqUFJ0
CMRw3J5QdCHojXohw0+WbhXRIjVhLfoIN+4Zba3bssx9BzT1YBkstTTZbyACMANx
sbqjYAuG7ZoIapVon+Kz4ZNkfF6Tpt95LY2F45TPI11xzPKwTdb+mciUqXWi4w==
-----END CERTIFICATE-----
)EOF";

enum class AppId : uint8_t { Info, Market, OneTap, Schedule, Voice, IR, Level, Tilt, Settings, Count };
struct AppEntry { const char* name; const char* title; };
constexpr AppEntry APPS[] = {
  {"信息台","信息台"}, {"市场资讯","市场资讯"}, {"一键乐","一键乐"},
  {"日程倒数","日程倒数"}, {"语音助手","语音助手"}, {"红外工具","红外工具"},
  {"水平仪","水平仪"}, {"倾斜游戏","倾斜游戏"}, {"设置","设置"}
};
constexpr uint8_t APP_COUNT = static_cast<uint8_t>(AppId::Count);

M5Canvas frame(&M5.Display);
uint8_t selected = 0;
bool inApp = false;
AppId current = AppId::Info;
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

String eventTitle;
time_t eventEpoch = 0;
String decisionOptions = "去做|先等等|换个方案|今天完成";

struct StockQuote { const char* symbol; float price; float changePct; bool valid; };
StockQuote stocks[] = {
  {"QQQ",0,0,false},{"SPY",0,0,false},{"SMH",0,0,false},{"NVDA",0,0,false},
  {"AMD",0,0,false},{"TSM",0,0,false},{"MSFT",0,0,false},{"GOOGL",0,0,false}
};
constexpr uint8_t STOCK_COUNT = sizeof(stocks)/sizeof(stocks[0]);
String stockError;

struct NewsItem { String title; String source; uint32_t publishedAt; };
NewsItem newsItems[8];
uint8_t newsCount = 0;
String newsError;

struct WeatherData {
  bool valid=false;
  String location;
  float temperature=0, apparent=0, wind=0;
  int humidity=0, weatherCode=-1;
} weather;
String weatherError;

struct RssPick { bool valid=false; String title; String source; String feedId; };
RssPick rssPick;
String rssError;

int16_t* voiceBuffer = nullptr;
constexpr uint32_t VOICE_RATE = 16000;
constexpr size_t VOICE_SAMPLES = 16000;
String voiceStatus = "长按A录音并回放";

RingbufHandle_t irRingBuffer = nullptr;
constexpr rmt_channel_t IR_RMT_CHANNEL = RMT_CHANNEL_0;
bool irActive=false, irHasFrame=false, irFrameValid=false, irRepeatFrame=false;
size_t irSymbolCount=0;
uint32_t irRawData=0;
uint16_t irAddress=0;
uint8_t irCommand=0;

struct Vec3 { float x,y,z; };
Vec3 levelRef{0,0,1};
bool levelCalibrated=false;
float levelTiltX=0, levelTiltY=0, levelTotal=0;
float gameX=67.0f, gameY=132.0f, gameVx=0, gameVy=0;

enum class OneTapMode : uint8_t {
  Dice, Coin, Fortune, RandomNews, Weather, RandomStock,
  Meal, RandomRss, Challenge, Reaction, DecisionWheel, Count
};
enum class ReactionState : uint8_t { Idle, Waiting, Ready };
OneTapMode oneTapMode = OneTapMode::Dice;
String oneTapMain="按 A", oneTapDetail="短按A触发 · 长按A换玩法";
String challengeName;
bool challengeActive=false, challengeFinishedNotified=false;
uint32_t challengeStartedAt=0;
ReactionState reactionState=ReactionState::Idle;
uint32_t reactionGoAt=0, reactionReadyAt=0;

int W(){return frame.width();}
int H(){return frame.height();}
void fontSmall(){frame.setFont(&fonts::efontCN_12);frame.setTextSize(1);}
void fontUI(){frame.setFont(&fonts::efontCN_16_b);frame.setTextSize(1);}
void fontBig(){frame.setFont(&fonts::efontCN_16_b);frame.setTextSize(2);}
void beginFrame(){frame.fillSprite(BG);frame.setTextWrap(false);frame.setTextDatum(top_left);frame.setTextColor(FG,BG);fontSmall();}
void present(){frame.pushSprite(0,0);}

size_t utf8GlyphLen(uint8_t c){if((c&0x80)==0)return 1;if((c&0xE0)==0xC0)return 2;if((c&0xF0)==0xE0)return 3;if((c&0xF8)==0xF0)return 4;return 1;}
void drawWrappedText(const String& text,int x,int y,int maxWidth,int lineHeight,int maxLines,uint16_t color=FG,uint16_t bg=BG){
  fontSmall();frame.setTextColor(color,bg);String line;int lines=0;
  auto flush=[&](){if(!line.length()||lines>=maxLines)return;frame.drawString(line,x,y+lines*lineHeight);line="";++lines;};
  for(size_t i=0;i<text.length()&&lines<maxLines;){if(text[i]=='\n'){flush();++i;continue;}size_t n=utf8GlyphLen((uint8_t)text[i]);if(i+n>text.length())n=1;String g=text.substring(i,i+n);i+=n;String c=line+g;if(line.length()&&frame.textWidth(c)>maxWidth){flush();if(lines>=maxLines)break;line=g;}else line=c;}if(lines<maxLines)flush();
}

bool wifiConnected(){return WiFi.status()==WL_CONNECTED;}
bool hasWiFiConfig(){return configuredSsid.length()>0;}
void drawBatteryIcon(int x,int y,int level,bool charging){const int w=24,h=12;frame.drawRoundRect(x,y,w,h,2,FG);frame.fillRect(x+w,y+4,2,4,FG);int pct=constrain(level,0,100),fw=(w-4)*pct/100;uint16_t c=pct<=15?TFT_RED:(pct<=30?WARN:OK);if(fw>0)frame.fillRoundRect(x+2,y+2,fw,h-4,1,c);if(charging){fontSmall();frame.setTextColor(TFT_BLACK,c);frame.drawString("+",x+9,y-1);}}
void drawStatusBar(const char* label="Pocket"){frame.fillRect(0,0,W(),28,BLUE);fontSmall();frame.setTextColor(FG,BLUE);frame.drawString(label,5,7);frame.fillCircle(75,14,4,wifiConnected()?OK:(setupPortalActive?WARN:MUTED));int bat=M5.Power.getBatteryLevel();bool ch=static_cast<bool>(M5.Power.isCharging());drawBatteryIcon(84,8,bat<0?0:bat,ch);frame.setTextColor(FG,BLUE);frame.drawString(bat>=0?String(bat):String("--"),112,7);}
void drawAppHeader(const char* title){drawStatusBar();fontUI();frame.setTextColor(ACCENT,BG);frame.drawString(title,7,34);frame.drawFastHLine(7,58,W()-14,MUTED);}
void drawFooter(const char* a="A 操作",const char* b="B 返回"){int y=H()-27;frame.fillRoundRect(5,y+2,W()-10,23,6,BLUE);fontSmall();frame.setTextColor(FG,BLUE);frame.drawString(a,9,y+6);int bw=frame.textWidth(b);frame.drawString(b,W()-9-bw,y+6);}
void drawPageTag(uint8_t page,uint8_t count){if(count<=1)return;fontSmall();frame.setTextColor(MUTED,BG);String p=String(page+1)+"/"+String(count);frame.drawString(p,W()-7-frame.textWidth(p),45);}
void drawInfoCard(int y,const char* label,const String& value,uint16_t valueColor=FG){frame.fillRoundRect(7,y,W()-14,44,8,PANEL);fontSmall();frame.setTextColor(MUTED,PANEL);frame.drawString(label,13,y+5);fontUI();frame.setTextColor(valueColor,PANEL);if(frame.textWidth(value)<=W()-28)frame.drawString(value,13,y+19);else{fontSmall();drawWrappedText(value,13,y+20,W()-28,13,2,valueColor,PANEL);}}
void drawBatteryCard(int y){int bat=M5.Power.getBatteryLevel(),mv=M5.Power.getBatteryVoltage();bool ch=static_cast<bool>(M5.Power.isCharging());int pct=bat<0?0:constrain(bat,0,100);frame.fillRoundRect(7,y,W()-14,37,8,PANEL);fontSmall();frame.setTextColor(MUTED,PANEL);frame.drawString(ch?"电量 · 充电中":"电量",13,y+4);if(mv>0)frame.drawString(String(mv/1000.0f,2)+"V",88,y+4);fontUI();frame.setTextColor(pct<=20?WARN:FG,PANEL);frame.drawString(bat>=0?String(bat)+"%":"--",13,y+14);int bx=58,by=y+20,bw=60;frame.drawRoundRect(bx,by,bw,9,2,MUTED);int fw=(bw-4)*pct/100;uint16_t c=pct<=15?TFT_RED:(pct<=30?WARN:OK);if(fw>0)frame.fillRoundRect(bx+2,by+2,fw,5,1,c);}

bool readTime(struct tm& t){return getLocalTime(&t,10);}
String hhmm(){struct tm t{};if(!readTime(t))return "--:--";char b[8];strftime(b,sizeof(b),"%H:%M",&t);return String(b);}
String dateLine(){struct tm t{};if(!readTime(t))return "时间尚未同步";static const char* wk[]={"周日","周一","周二","周三","周四","周五","周六"};return String(t.tm_mon+1)+"月"+String(t.tm_mday)+"日 "+wk[t.tm_wday];}
String formatEventTime(time_t epoch){if(epoch<=0)return "未设置";struct tm t{};localtime_r(&epoch,&t);char b[32];snprintf(b,sizeof(b),"%d月%d日 %02d:%02d",t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min);return String(b);}
String randomString(size_t length,const char* alphabet){String o;size_t n=strlen(alphabet);o.reserve(length);for(size_t i=0;i<length;++i)o+=alphabet[esp_random()%n];return o;}
time_t parseLocalDateTime(const String& value){if(value.length()<16)return 0;struct tm t{};t.tm_year=value.substring(0,4).toInt()-1900;t.tm_mon=value.substring(5,7).toInt()-1;t.tm_mday=value.substring(8,10).toInt();t.tm_hour=value.substring(11,13).toInt();t.tm_min=value.substring(14,16).toInt();t.tm_isdst=-1;return mktime(&t);}

float vdot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 vcross(const Vec3&a,const Vec3&b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 vscale(const Vec3&a,float s){return {a.x*s,a.y*s,a.z*s};}
Vec3 vsub(const Vec3&a,const Vec3&b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float vnorm(const Vec3&a){return sqrtf(vdot(a,a));}
Vec3 vnormalize(Vec3 a){float n=vnorm(a);if(n<1e-5f)return {0,0,1};return vscale(a,1.0f/n);}
Vec3 readAccel(){Vec3 g{0,0,1};if(M5.Imu.update()){auto d=M5.Imu.getImuData();g={d.accel.x,d.accel.y,d.accel.z};}return vnormalize(g);}
void levelBasis(Vec3& right,Vec3& down){Vec3 candidate=fabsf(levelRef.x)<0.88f?Vec3{1,0,0}:Vec3{0,1,0};right=vnormalize(vsub(candidate,vscale(levelRef,vdot(candidate,levelRef))));down=vnormalize(vcross(levelRef,right));}
void saveLevelRef(){Preferences p;if(p.begin("pocket-nexus",false)){p.putFloat("lvl_x",levelRef.x);p.putFloat("lvl_y",levelRef.y);p.putFloat("lvl_z",levelRef.z);p.putBool("lvl_ok",true);p.end();}}
void calibrateLevel(){levelRef=readAccel();levelCalibrated=true;saveLevelRef();}

void loadSettings(){
  if(strlen(PN_WIFI_SSID)>0){configuredSsid=PN_WIFI_SSID;configuredPassword=PN_WIFI_PASSWORD;}
  Preferences p;if(p.begin("pocket-nexus",true)){
    if(!configuredSsid.length()){configuredSsid=p.getString("ssid","");configuredPassword=p.getString("pass","");}
    eventTitle=p.getString("evt_title","");eventEpoch=(time_t)p.getLong64("evt_epoch",0);
    decisionOptions=p.getString("decision","去做|先等等|换个方案|今天完成");
    levelCalibrated=p.getBool("lvl_ok",false);if(levelCalibrated){levelRef={p.getFloat("lvl_x",0),p.getFloat("lvl_y",0),p.getFloat("lvl_z",1)};levelRef=vnormalize(levelRef);}p.end();
  }
  if(!decisionOptions.length())decisionOptions="去做|先等等|换个方案|今天完成";
}
void saveSettings(const String& ssid,const String& pass,const String& evt,time_t epoch,const String& decision){Preferences p;if(p.begin("pocket-nexus",false)){p.putString("ssid",ssid);p.putString("pass",pass);p.putString("evt_title",evt);p.putLong64("evt_epoch",(int64_t)epoch);p.putString("decision",decision);p.end();}}

void addSetupSecurityHeaders(){setupServer.sendHeader("Cache-Control","no-store, no-cache, must-revalidate");setupServer.sendHeader("Pragma","no-cache");setupServer.sendHeader("X-Content-Type-Options","nosniff");setupServer.sendHeader("X-Frame-Options","DENY");setupServer.sendHeader("Referrer-Policy","no-referrer");setupServer.sendHeader("Content-Security-Policy","default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");}
String htmlEscape(const String&s){String o;for(size_t i=0;i<s.length();++i){char c=s[i];if(c=='&')o+="&amp;";else if(c=='<')o+="&lt;";else if(c=='>')o+="&gt;";else if(c=='\"')o+="&quot;";else if(c=='\'')o+="&#39;";else o+=c;}return o;}
String setupPageHtml(){String p;p.reserve(3600);p+=F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Pocket Nexus</title><style>body{font-family:system-ui;background:#0b0d10;color:#fff;padding:24px}main{max-width:520px;margin:auto;background:#151920;padding:24px;border-radius:18px}input,textarea,button{box-sizing:border-box;width:100%;font-size:16px;padding:12px;margin:7px 0;border-radius:10px}input,textarea{background:#0f1318;color:white;border:1px solid #39434f}textarea{min-height:84px;resize:vertical}button{border:0;background:#35d0ba;color:#07110f;font-weight:700}label{display:block;color:#aeb8c5;margin-top:10px}p{color:#aeb8c5;line-height:1.5}</style></head><body><main><h2>Pocket Nexus 配置</h2><p>仅在临时安全热点中开放，设置保存在设备本机。</p><form method='post' action='/save'><input type='hidden' name='token' value='");p+=setupToken;p+=F("'><label>2.4 GHz Wi-Fi</label><input name='ssid' maxlength='32' required value='");p+=htmlEscape(configuredSsid);p+=F("'><input name='password' type='password' maxlength='63' placeholder='留空保留原密码'><label>最近事件标题</label><input name='event_title' maxlength='48' placeholder='例如：例行洁牙' value='");p+=htmlEscape(eventTitle);p+=F("'><label>事件时间</label><input name='event_time' type='datetime-local'><label>决策轮盘候选项</label><textarea name='decision_options' maxlength='240' placeholder='例如：去吃日料|吃中餐|在家做饭'>");p+=htmlEscape(decisionOptions);p+=F("</textarea><p>使用 | 分隔 2–10 个候选项。</p><button type='submit'>保存并重启</button></form><p>热点最多 1 台设备，10 分钟自动关闭。</p></main></body></html>");return p;}
void renderSetupPortal(){beginFrame();drawStatusBar("安全配置");fontUI();frame.setTextColor(WARN,BG);frame.drawString("网络 / 日程 / 轮盘",7,35);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString("热点",8,68);frame.setTextColor(FG,BG);drawWrappedText(setupApName,8,83,W()-16,14,2);frame.setTextColor(MUTED,BG);frame.drawString("密码",8,113);frame.setTextColor(ACCENT,BG);drawWrappedText(setupApPassword,8,128,W()-16,14,2,ACCENT);frame.setTextColor(MUTED,BG);frame.drawString("浏览器：192.168.4.1",8,165);frame.drawString("1台设备 · 10分钟",8,187);drawFooter("A -","B 取消");present();}
void installSetupRoutes(){if(setupRoutesInstalled)return;setupRoutesInstalled=true;setupServer.on("/",HTTP_GET,[](){addSetupSecurityHeaders();setupServer.send(200,"text/html; charset=utf-8",setupPageHtml());});setupServer.on("/save",HTTP_POST,[](){addSetupSecurityHeaders();String token=setupServer.arg("token"),ssid=setupServer.arg("ssid"),pass=setupServer.arg("password"),evt=setupServer.arg("event_title"),evtTime=setupServer.arg("event_time"),decision=setupServer.arg("decision_options");if(token!=setupToken){setupServer.send(403,"text/plain","Invalid token");return;}if(!ssid.length()||ssid.length()>32||pass.length()>63||evt.length()>48||decision.length()>240){setupServer.send(400,"text/plain","Invalid settings");return;}if(pass.length()==0&&ssid==configuredSsid)pass=configuredPassword;if(!decision.length())decision="去做|先等等|换个方案|今天完成";time_t epoch=evtTime.length()?parseLocalDateTime(evtTime):eventEpoch;saveSettings(ssid,pass,evt,epoch,decision);setupServer.send(200,"text/html; charset=utf-8","<html><meta charset='utf-8'><body><h2>已保存</h2><p>设备正在重启。</p></body></html>");delay(700);ESP.restart();});setupServer.onNotFound([](){addSetupSecurityHeaders();setupServer.sendHeader("Location","/",true);setupServer.send(302,"text/plain","");});}
void stopSetupPortal(){if(!setupPortalActive)return;setupServer.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);setupPortalActive=false;setupApPassword="";setupToken="";}
void startSetupPortal(){if(setupPortalActive)return;static const char* safe="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";static const char* hex="0123456789abcdef";char suffix[5];snprintf(suffix,sizeof(suffix),"%04X",(uint16_t)(ESP.getEfuseMac()&0xFFFF));setupApName=String("PN-Setup-")+suffix;setupApPassword=randomString(12,safe);setupToken=randomString(24,hex);setupStartedAt=millis();WiFi.disconnect(true,false);delay(50);WiFi.mode(WIFI_AP);WiFi.softAP(setupApName.c_str(),setupApPassword.c_str(),1,false,1);setupPortalActive=true;installSetupRoutes();setupServer.begin();renderSetupPortal();}

void beginWifiAttempt(){if(!hasWiFiConfig()||setupPortalActive)return;lastWifiAttempt=millis();WiFi.mode(WIFI_STA);WiFi.begin(configuredSsid.c_str(),configuredPassword.c_str());}
void serviceWiFi(){if(setupPortalActive||!hasWiFiConfig())return;if(wifiConnected()){if(!ntpConfigured){configTzTime(PN_TIMEZONE,"pool.ntp.org","time.nist.gov");ntpConfigured=true;}return;}if(millis()-lastWifiAttempt>=WIFI_RETRY_MS)beginWifiAttempt();}
void connectWiFi(){loadSettings();WiFi.persistent(false);WiFi.setAutoReconnect(true);setenv("TZ",PN_TIMEZONE,1);tzset();if(!hasWiFiConfig()){startSetupPortal();return;}beginWifiAttempt();uint32_t s=millis();while(!wifiConnected()&&millis()-s<9000){M5.update();delay(50);}if(wifiConnected()){configTzTime(PN_TIMEZONE,"pool.ntp.org","time.nist.gov");ntpConfigured=true;}}

bool httpsGetJson(const String& path,DynamicJsonDocument& doc,String& err){if(!wifiConnected()){err="无线网络未连接";return false;}WiFiClientSecure client;client.setCACert(GTS_ROOT_R4);HTTPClient http;http.setTimeout(10000);String url=String(POCKET_API)+path;if(!http.begin(client,url)){err="HTTPS 初始化失败";return false;}int code=http.GET();if(code!=HTTP_CODE_OK){err=String("HTTP ")+code;http.end();return false;}DeserializationError e=deserializeJson(doc,http.getStream());http.end();if(e){err="JSON 解析失败";return false;}return true;}
bool fetchStocks(){stockError="";DynamicJsonDocument doc(12288);if(!httpsGetJson("/api/stocks",doc,stockError))return false;for(auto&q:stocks)q.valid=false;for(JsonObject row:doc["items"].as<JsonArray>()){const char* sym=row["symbol"]|"";for(auto&q:stocks)if(strcmp(sym,q.symbol)==0){q.price=row["price"]|0.0f;q.changePct=row["changePct"]|0.0f;q.valid=true;}}return true;}
bool fetchNews(){newsError="";DynamicJsonDocument doc(16384);if(!httpsGetJson("/api/news",doc,newsError))return false;newsCount=0;for(JsonObject row:doc["items"].as<JsonArray>()){if(newsCount>=8)break;newsItems[newsCount].title=String((const char*)(row["title"]|""));newsItems[newsCount].source=String((const char*)(row["source"]|""));newsItems[newsCount].publishedAt=row["publishedAt"]|0;newsCount++;}return newsCount>0;}
bool fetchWeather(){weatherError="";DynamicJsonDocument doc(8192);if(!httpsGetJson("/api/weather",doc,weatherError)){weather.valid=false;return false;}weather.location=String((const char*)(doc["location"]|"当前位置"));weather.temperature=doc["temperature"]|0.0f;weather.apparent=doc["apparent"]|0.0f;weather.humidity=doc["humidity"]|0;weather.wind=doc["wind"]|0.0f;weather.weatherCode=doc["weatherCode"]|-1;weather.valid=true;return true;}
bool fetchRandomRss(){rssError="";DynamicJsonDocument doc(12288);if(!httpsGetJson("/api/rss-random",doc,rssError)){rssPick.valid=false;return false;}JsonObject item=doc["item"].as<JsonObject>();rssPick.title=String((const char*)(item["title"]|""));rssPick.source=String((const char*)(item["source"]|"RSS Orbit"));rssPick.feedId=String((const char*)(item["feedId"]|""));rssPick.valid=rssPick.title.length()>0;if(!rssPick.valid)rssError="RSS 内容为空";return rssPick.valid;}
String weatherLabel(int code){if(code==0)return "晴";if(code<=2)return "少云";if(code==3)return "阴";if(code==45||code==48)return "雾";if(code>=51&&code<=67)return "雨";if(code>=71&&code<=77)return "雪";if(code>=80&&code<=82)return "阵雨";if(code>=85&&code<=86)return "阵雪";if(code>=95)return "雷雨";return "未知";}

void renderMenu(){beginFrame();drawStatusBar();fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(String("应用 ")+String(selected+1)+"/"+String(APP_COUNT),7,33);constexpr uint8_t rows=4;uint8_t start=selected>=rows?selected-rows+1:0;for(uint8_t r=0;r<rows;++r){uint8_t idx=start+r;if(idx>=APP_COUNT)break;int y=48+r*39;bool active=idx==selected;frame.fillRoundRect(5,y,W()-10,34,7,active?BLUE:PANEL2);if(!active)frame.drawRoundRect(5,y,W()-10,34,7,MUTED);fontSmall();frame.setTextColor(active?FG:MUTED,active?BLUE:PANEL2);frame.drawString(String(idx+1),10,y+8);fontUI();frame.setTextColor(FG,active?BLUE:PANEL2);frame.drawString(APPS[idx].name,34,y+5);}drawFooter("A 下一项","B 打开");present();}

void renderInfo(){beginFrame();drawAppHeader("信息台");drawPageTag(pageIndex,2);if(pageIndex==0){fontBig();frame.setTextColor(ACCENT,BG);frame.drawString(hhmm(),8,68);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(dateLine(),9,110);String evt=(eventTitle.length()&&eventEpoch>0)?eventTitle:"暂无事件";drawInfoCard(132,"最近事件",evt,eventEpoch>time(nullptr)?FG:MUTED);drawBatteryCard(180);}else{drawInfoCard(68,"无线网络",wifiConnected()?"已连接":"未连接",wifiConnected()?OK:WARN);drawInfoCard(116,"信号",wifiConnected()?String(WiFi.RSSI())+" dBm":"--");drawInfoCard(164,"IP",wifiConnected()?WiFi.localIP().toString():"--");}drawFooter("A 下一页","B 返回");present();}

void renderMarket(){beginFrame();drawAppHeader("市场资讯");drawPageTag(pageIndex,3);if(pageIndex<2){uint8_t start=pageIndex*4;for(uint8_t i=0;i<4;++i){StockQuote&q=stocks[start+i];int y=68+i*33;frame.fillRoundRect(7,y,W()-14,29,6,PANEL);fontUI();frame.setTextColor(FG,PANEL);frame.drawString(q.symbol,12,y+5);fontSmall();if(q.valid){String p=String(q.price,2);frame.drawString(p,52,y+7);String pct=String(q.changePct>=0?"+":"")+String(q.changePct,2)+"%";frame.setTextColor(q.changePct>=0?OK:TFT_RED,PANEL);frame.drawString(pct,W()-10-frame.textWidth(pct),y+7);}else{frame.setTextColor(MUTED,PANEL);frame.drawString("--",70,y+7);}}fontSmall();frame.setTextColor(stockError.length()?WARN:MUTED,BG);drawWrappedText(stockError.length()?stockError:"长按A联网刷新",8,202,W()-16,13,1,stockError.length()?WARN:MUTED);}else{if(newsCount==0){fontUI();frame.setTextColor(WARN,BG);frame.drawString(newsError.length()?"新闻读取失败":"尚无新闻",8,80);fontSmall();drawWrappedText(newsError.length()?newsError:"长按A刷新市场资讯",8,120,W()-16,15,5,FG);}else{fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(newsItems[0].source,8,70);fontUI();frame.setTextColor(FG,BG);drawWrappedText(newsItems[0].title,8,94,W()-16,19,6,FG);}}drawFooter("A 下一页","B 返回");present();}

const char* oneTapModeName(OneTapMode m){switch(m){case OneTapMode::Dice:return "骰子";case OneTapMode::Coin:return "抛硬币";case OneTapMode::Fortune:return "今日运势";case OneTapMode::RandomNews:return "随机新闻";case OneTapMode::Weather:return "刷新天气";case OneTapMode::RandomStock:return "随机股票";case OneTapMode::Meal:return "今天吃什么";case OneTapMode::RandomRss:return "随机 RSS";case OneTapMode::Challenge:return "30秒挑战";case OneTapMode::Reaction:return "反应测试";case OneTapMode::DecisionWheel:return "决策轮盘";default:return "一键乐";}}
void renderOneTap(){beginFrame();drawAppHeader("一键乐");fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(oneTapModeName(oneTapMode),8,70);fontBig();frame.setTextColor(ACCENT,BG);drawWrappedText(oneTapMain,8,94,W()-16,30,3,ACCENT);fontSmall();frame.setTextColor(FG,BG);drawWrappedText(oneTapDetail,8,164,W()-16,15,3,FG);drawFooter("A 触发","长A 换玩法");present();}
void resetOneTapTransient(){challengeActive=false;challengeFinishedNotified=false;reactionState=ReactionState::Idle;reactionGoAt=reactionReadyAt=0;}
void nextOneTapMode(){resetOneTapTransient();uint8_t n=(static_cast<uint8_t>(oneTapMode)+1)%static_cast<uint8_t>(OneTapMode::Count);oneTapMode=(OneTapMode)n;oneTapMain="按 A";oneTapDetail="短按A触发 · 长按A换玩法";renderOneTap();}
String pickDelimitedOption(const String& value){uint8_t count=0;int start=0;while(start<value.length()){int end=value.indexOf('|',start);if(end<0)end=value.length();String part=value.substring(start,end);part.trim();if(part.length())count++;start=end+1;}if(count==0)return "再想想";uint8_t target=esp_random()%count;start=0;uint8_t idx=0;while(start<value.length()){int end=value.indexOf('|',start);if(end<0)end=value.length();String part=value.substring(start,end);part.trim();if(part.length()){if(idx==target)return part;idx++;}start=end+1;}return "再想想";}
void startChallenge(){static const char* items[]={"深蹲","开合跳","高抬腿","平板支撑","靠墙静蹲","原地快步","抬膝触掌","肩颈拉伸","踮脚提踵","原地小步跑"};challengeName=items[esp_random()%(sizeof(items)/sizeof(items[0]))];challengeActive=true;challengeFinishedNotified=false;challengeStartedAt=millis();oneTapMain="30秒";oneTapDetail=challengeName+" · 开始！";}
void handleReactionTap(){uint32_t now=millis();if(reactionState==ReactionState::Idle){reactionState=ReactionState::Waiting;reactionGoAt=now+1800+(esp_random()%3001);reactionReadyAt=0;oneTapMain="等...";oneTapDetail="看到“按！”再按 A";return;}if(reactionState==ReactionState::Waiting){if((int32_t)(now-reactionGoAt)<0){reactionState=ReactionState::Idle;oneTapMain="太早了";oneTapDetail="抢跑 · 再按一次重新开始";return;}reactionState=ReactionState::Ready;reactionReadyAt=reactionGoAt;}if(reactionState==ReactionState::Ready){uint32_t ms=now-reactionReadyAt;reactionState=ReactionState::Idle;oneTapMain=String(ms)+" ms";if(ms<180)oneTapDetail="非常快";else if(ms<240)oneTapDetail="很快";else if(ms<320)oneTapDetail="不错";else oneTapDetail="再试一次，看看能不能更快";}}
void triggerOneTap(){
  static const char* fortunes[]={"大吉 · 适合开始新事情","中吉 · 稳扎稳打更顺","小吉 · 会有一个小惊喜","宜专注 · 少开几个坑","宜清理 · 先解决积压","宜学习 · 今天吸收很快","宜行动 · 别再多想一轮","宜休息 · 留点电给明天","财运一般 · 别追高","手气不错 · 可以试试新东西","人品在线 · 多帮一个人","随机之神说：再按一次"};
  static const char* meals[]={"番茄鸡蛋面","日式亲子丼","照烧鸡饭","寿喜烧乌冬","韩式拌饭","越南河粉","海南鸡饭","清汤馄饨","番茄牛腩","咖喱鸡饭","冷荞麦面","烤三文鱼","豆腐蔬菜锅","虾仁炒饭","蒸鱼配米饭","牛肉乌冬","鸡丝粥配小菜","麻婆豆腐配米饭","饭团配味噌汤","空气炸锅鸡腿配蔬菜"};
  M5.Speaker.tone(1900,35);
  switch(oneTapMode){
    case OneTapMode::Dice:{int v=1+(esp_random()%6);oneTapMain=String(v);oneTapDetail="1–6 点 · 再按一次重掷";break;}
    case OneTapMode::Coin:{bool h=esp_random()&1;oneTapMain=h?"正面":"反面";oneTapDetail="50 / 50 · 再按一次重抛";break;}
    case OneTapMode::Fortune:{int score=55+(esp_random()%46);const char* f=fortunes[esp_random()%(sizeof(fortunes)/sizeof(fortunes[0]))];oneTapMain=String(score)+"分";oneTapDetail=String(f)+"\n仅供娱乐";break;}
    case OneTapMode::RandomNews:{if(newsCount==0&&!fetchNews()){oneTapMain="读取失败";oneTapDetail=newsError;break;}uint8_t i=esp_random()%newsCount;oneTapMain=newsItems[i].source;oneTapDetail=newsItems[i].title;break;}
    case OneTapMode::Weather:{if(!fetchWeather()){oneTapMain="天气失败";oneTapDetail=weatherError;break;}oneTapMain=String(weather.temperature,0)+"°F "+weatherLabel(weather.weatherCode);oneTapDetail=weather.location+" · 体感 "+String(weather.apparent,0)+"°F\n湿度 "+String(weather.humidity)+"% · 风 "+String(weather.wind,0)+" mph";break;}
    case OneTapMode::RandomStock:{if(!stocks[0].valid&&!fetchStocks()){oneTapMain="行情失败";oneTapDetail=stockError;break;}uint8_t i=esp_random()%STOCK_COUNT;oneTapMain=stocks[i].symbol;oneTapDetail=stocks[i].valid?String(stocks[i].price,2)+" · "+String(stocks[i].changePct>=0?"+":"")+String(stocks[i].changePct,2)+"%":"暂无行情";break;}
    case OneTapMode::Meal:{oneTapMain=meals[esp_random()%(sizeof(meals)/sizeof(meals[0]))];oneTapDetail="不满意就再按一次";break;}
    case OneTapMode::RandomRss:{if(!fetchRandomRss()){oneTapMain="RSS 失败";oneTapDetail=rssError;break;}oneTapMain=rssPick.source;oneTapDetail=rssPick.title;break;}
    case OneTapMode::Challenge:{startChallenge();break;}
    case OneTapMode::Reaction:{handleReactionTap();break;}
    case OneTapMode::DecisionWheel:{oneTapMain=pickDelimitedOption(decisionOptions);oneTapDetail="候选项可在设置页修改";break;}
    default:break;
  }
  renderOneTap();
}

void renderSchedule(){beginFrame();drawAppHeader("日程倒数");if(eventEpoch<=0||!eventTitle.length()){fontUI();frame.setTextColor(WARN,BG);frame.drawString("尚未设置事件",8,82);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("在“设置”里长按侧键B，打开安全配置页后填写事件标题和时间。",8,126,W()-16,15,6,FG);}else{time_t now=time(nullptr);long long d=(long long)eventEpoch-(long long)now;bool overdue=d<0;if(overdue)d=-d;long long days=d/86400LL;int hours=(d%86400LL)/3600LL;int mins=(d%3600LL)/60LL;fontSmall();frame.setTextColor(MUTED,BG);drawWrappedText(eventTitle,8,70,W()-16,15,2,MUTED);fontBig();frame.setTextColor(overdue?WARN:ACCENT,BG);frame.drawString(String(days)+"天",8,112);fontUI();frame.drawString(String(hours)+"小时 "+String(mins)+"分",8,157);fontSmall();frame.setTextColor(FG,BG);frame.drawString(formatEventTime(eventEpoch),8,190);}drawFooter("A 刷新","B 返回");present();}

bool recordVoiceLoopback(){if(!voiceBuffer)voiceBuffer=(int16_t*)heap_caps_malloc(VOICE_SAMPLES*sizeof(int16_t),MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM);if(!voiceBuffer){voiceStatus="内存不足";return false;}voiceStatus="录音中…";beginFrame();drawAppHeader("语音助手");fontUI();frame.setTextColor(WARN,BG);frame.drawString(voiceStatus,8,90);present();M5.Speaker.end();M5.Mic.begin();constexpr size_t block=320;size_t pos=0;while(pos<VOICE_SAMPLES){size_t n=min(block,VOICE_SAMPLES-pos);if(M5.Mic.record(voiceBuffer+pos,n,VOICE_RATE))pos+=n;M5.update();delay(1);}while(M5.Mic.isRecording())delay(1);M5.Mic.end();voiceStatus="回放中…";M5.Speaker.begin();M5.Speaker.playRaw(voiceBuffer,VOICE_SAMPLES,VOICE_RATE,false,1,0);while(M5.Speaker.isPlaying()){M5.update();delay(1);}voiceStatus="麦克风 / 扬声器正常";return true;}
void renderVoice(){beginFrame();drawAppHeader("语音助手");fontUI();frame.setTextColor(ACCENT,BG);drawWrappedText(voiceStatus,8,78,W()-16,21,4,ACCENT);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("目前先做本机录音回放测试。真正 AI 对话会等设备认证层完成后再接入。",8,135,W()-16,15,5,FG);drawFooter("长按A 录音","B 返回");present();}

bool decodeNEC(const rmt_item32_t*s,size_t count,uint32_t*raw,bool*repeat){*raw=0;*repeat=false;if(count<2)return false;uint32_t h0=s[0].duration0,h1=s[0].duration1;if(h0>8000&&h1>4000){if(count<33)return false;}else if(h0>8000&&h1>2000&&h1<3000){*repeat=true;return false;}else return false;for(int i=0;i<32;++i){uint32_t m=s[i+1].duration0,sp=s[i+1].duration1;if(m<300||m>800)return false;if(sp>1000)*raw|=(1UL<<i);}uint8_t c=(*raw>>16)&0xFF,iv=(*raw>>24)&0xFF;return(c^iv)==0xFF;}
bool startIr(){if(irActive)return true;M5.Speaker.end();M5.Power.setExtOutput(true,m5::ext_none);rmt_config_t c={};c.rmt_mode=RMT_MODE_RX;c.channel=IR_RMT_CHANNEL;c.gpio_num=(gpio_num_t)IR_RECEIVE_PIN;c.clk_div=80;c.mem_block_num=2;c.rx_config.filter_en=true;c.rx_config.filter_ticks_thresh=80;c.rx_config.idle_threshold=15000;if(rmt_config(&c)!=ESP_OK||rmt_driver_install(IR_RMT_CHANNEL,2048,0)!=ESP_OK||rmt_get_ringbuf_handle(IR_RMT_CHANNEL,&irRingBuffer)!=ESP_OK||!irRingBuffer||rmt_rx_start(IR_RMT_CHANNEL,true)!=ESP_OK){rmt_driver_uninstall(IR_RMT_CHANNEL);irRingBuffer=nullptr;M5.Power.setExtOutput(false,m5::ext_none);M5.Speaker.begin();return false;}irActive=true;irHasFrame=irFrameValid=irRepeatFrame=false;return true;}
void stopIr(){if(!irActive)return;rmt_rx_stop(IR_RMT_CHANNEL);rmt_driver_uninstall(IR_RMT_CHANNEL);irRingBuffer=nullptr;M5.Power.setExtOutput(false,m5::ext_none);M5.Speaker.begin();irActive=false;}
bool pollIr(){if(!irActive||!irRingBuffer)return false;size_t sz=0;auto*items=(rmt_item32_t*)xRingbufferReceive(irRingBuffer,&sz,0);if(!items)return false;size_t count=sz/sizeof(rmt_item32_t);uint32_t raw=0;bool rep=false;bool valid=decodeNEC(items,count,&raw,&rep);irHasFrame=true;irFrameValid=valid;irRepeatFrame=rep;irSymbolCount=count;irRawData=raw;irAddress=raw&0xFFFF;irCommand=(raw>>16)&0xFF;vRingbufferReturnItem(irRingBuffer,(void*)items);return true;}
void renderIR(){beginFrame();drawAppHeader("红外工具");drawPageTag(pageIndex,2);if(!irActive){fontUI();frame.setTextColor(WARN,BG);frame.drawString("红外接收未启动",8,82);}else if(pageIndex==0){if(!irHasFrame){fontUI();frame.setTextColor(ACCENT,BG);drawWrappedText("将遥控器对准设备",8,76,W()-16,20,3,ACCENT);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("按任意键，建议距离至少约30厘米。接收期间扬声器功放关闭。",8,135,W()-16,15,5,FG);}else if(irFrameValid){drawInfoCard(72,"协议","NEC",OK);drawInfoCard(120,"地址",String("0x")+String(irAddress,HEX));drawInfoCard(168,"指令",String("0x")+String(irCommand,HEX));}else{fontUI();frame.setTextColor(WARN,BG);frame.drawString(irRepeatFrame?"NEC 重复码":"收到原始信号",8,80);fontSmall();drawWrappedText(String("捕获 ")+String(irSymbolCount)+" 个 RMT 符号",8,125,W()-16,15,4,FG);}}else{frame.fillRoundRect(7,70,W()-14,120,8,PANEL);fontSmall();frame.setTextColor(FG,PANEL);String b=!irHasFrame?"正在等待红外信号。":(irFrameValid?String("协议: NEC\n地址: 0x")+String(irAddress,HEX)+"\n指令: 0x"+String(irCommand,HEX)+"\n原始码: 0x"+String(irRawData,HEX)+"\n符号: "+String(irSymbolCount):String("已捕获但未通过 NEC 校验\n符号: ")+String(irSymbolCount));drawWrappedText(b,13,82,W()-26,15,8,FG,PANEL);}drawFooter("A 下一页","B 返回");present();}

void updateLevelValues(){Vec3 g=readAccel();if(!levelCalibrated){levelRef=g;levelCalibrated=true;saveLevelRef();}Vec3 right,down;levelBasis(right,down);float dx=constrain(vdot(g,right),-1.0f,1.0f),dy=constrain(vdot(g,down),-1.0f,1.0f),dd=constrain(vdot(g,levelRef),-1.0f,1.0f);levelTiltX=asinf(dx)*180.0f/PI;levelTiltY=asinf(dy)*180.0f/PI;levelTotal=acosf(dd)*180.0f/PI;}
void renderLevel(){updateLevelValues();beginFrame();drawAppHeader("水平仪");fontSmall();frame.setTextColor(FG,BG);frame.drawString(String("左右 ")+String(levelTiltX,1)+"°",8,68);frame.drawString(String("前后 ")+String(levelTiltY,1)+"°",8,85);frame.setTextColor(levelTotal<1.0f?OK:(levelTotal<3.0f?WARN:FG),BG);frame.drawString(String("偏差 ")+String(levelTotal,1)+"°",8,102);int cx=W()/2,cy=157,R=41;frame.drawCircle(cx,cy,R,MUTED);frame.drawCircle(cx,cy,20,MUTED);frame.drawFastHLine(cx-R+5,cy,R*2-10,MUTED);frame.drawFastVLine(cx,cy-R+5,R*2-10,MUTED);const float maxDeg=15.0f;int bx=constrain(cx-(int)(levelTiltX/maxDeg*(R-8)),cx-R+8,cx+R-8);int by=constrain(cy-(int)(levelTiltY/maxDeg*(R-8)),cy-R+8,cy+R-8);frame.fillCircle(bx,by,7,levelTotal<1.0f?OK:ACCENT);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString("A 将当前姿态设为水平",8,202);drawFooter("A 校准","B 返回");present();}

void renderTilt(){Vec3 g=readAccel();gameVx=constrain(gameVx+g.x*.28f,-3.5f,3.5f);gameVy=constrain(gameVy+g.y*.28f,-3.5f,3.5f);gameVx*=.96f;gameVy*=.96f;gameX=constrain(gameX+gameVx,14.0f,(float)W()-14);gameY=constrain(gameY+gameVy,76.0f,(float)H()-45);beginFrame();drawAppHeader("倾斜游戏");frame.drawRoundRect(7,66,W()-14,H()-109,8,MUTED);frame.fillCircle((int)gameX,(int)gameY,7,ACCENT);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString("倾斜设备移动小球",8,194);drawFooter("A 重置","B 返回");present();}

void renderSaverPixelSafe(){
#if PN_HAS_RGB_SAVER
  M5.Display.fillScreen(TFT_BLACK);
  int ox=(M5.Display.width()-PN_SCREENSAVER_WIDTH)/2,oy=(M5.Display.height()-PN_SCREENSAVER_HEIGHT)/2;
  M5.Display.startWrite();
  for(int y=0;y<PN_SCREENSAVER_HEIGHT;++y){for(int x=0;x<PN_SCREENSAVER_WIDTH;++x){uint16_t v=pgm_read_word(&PN_SCREENSAVER_PIXELS[y*PN_SCREENSAVER_WIDTH+x]);uint8_t r=(uint8_t)(((v>>11)&0x1F)*255/31);uint8_t g=(uint8_t)(((v>>5)&0x3F)*255/63);uint8_t b=(uint8_t)((v&0x1F)*255/31);M5.Display.writePixel(ox+x,oy+y,M5.Display.color565(r,g,b));}}
  M5.Display.endWrite();
#else
  M5.Display.fillScreen(TFT_BLACK);M5.Display.setFont(&fonts::efontCN_16_b);M5.Display.setTextDatum(middle_center);M5.Display.setTextColor(TFT_WHITE);M5.Display.setTextSize(2);M5.Display.drawString(hhmm(),M5.Display.width()/2,92);M5.Display.setTextSize(1);M5.Display.drawString(dateLine(),M5.Display.width()/2,135);M5.Display.setTextDatum(top_left);
#endif
}
void activateSaver(){if(setupPortalActive||irActive)return;screensaverActive=true;renderSaverPixelSafe();}
void leaveSaver(){if(!screensaverActive)return;screensaverActive=false;lastInteractionAt=millis();renderMenu();}

void renderSettings(){beginFrame();drawAppHeader("设置");drawPageTag(pageIndex,2);if(pageIndex==0){drawInfoCard(68,"无线网络",wifiConnected()?"已连接":(hasWiFiConfig()?"正在重连":"未配置"),wifiConnected()?OK:WARN);drawInfoCard(116,"网络名称",wifiConnected()?WiFi.SSID():(hasWiFiConfig()?configuredSsid:"无"));fontSmall();frame.setTextColor(MUTED,BG);drawWrappedText("长按侧键B：配置 Wi-Fi、日程和决策轮盘候选项。",8,171,W()-16,14,3,MUTED);}else{fontUI();frame.setTextColor(ACCENT,BG);frame.drawString(PN_HAS_RGB_SAVER?"自定义图片已载入":"默认时钟屏保",8,78);fontSmall();frame.setTextColor(FG,BG);drawWrappedText("逐像素安全写屏。无操作60秒自动进入屏保。",8,120,W()-16,15,5,FG);frame.setTextColor(MUTED,BG);frame.drawString("长按A立即预览",8,194);}drawFooter("A 下一页","B 返回");present();}

uint8_t marketPageCount(){return 3;}
void renderCurrent(){lastRender=millis();switch(current){case AppId::Info:renderInfo();break;case AppId::Market:renderMarket();break;case AppId::OneTap:renderOneTap();break;case AppId::Schedule:renderSchedule();break;case AppId::Voice:renderVoice();break;case AppId::IR:renderIR();break;case AppId::Level:renderLevel();break;case AppId::Tilt:renderTilt();break;case AppId::Settings:renderSettings();break;default:break;}}
void enterSelected(){current=(AppId)selected;inApp=true;pageIndex=0;if(current==AppId::Market){if(!stocks[0].valid)fetchStocks();if(newsCount==0)fetchNews();}if(current==AppId::IR)startIr();if(current==AppId::Level&&!levelCalibrated)calibrateLevel();if(current==AppId::Tilt){gameX=W()/2.0f;gameY=H()/2.0f;gameVx=gameVy=0;}if(current==AppId::OneTap){resetOneTapTransient();oneTapMain="按 A";oneTapDetail="短按A触发 · 长按A换玩法";}renderCurrent();}
void leaveApp(){if(current==AppId::IR)stopIr();if(current==AppId::OneTap)resetOneTapTransient();inApp=false;pageIndex=0;renderMenu();}
void refreshMarket(){fetchStocks();fetchNews();renderMarket();}

void handleButtons(){bool ac=M5.BtnA.wasClicked(),ah=M5.BtnA.wasHold(),bc=M5.BtnB.wasClicked(),bh=M5.BtnB.wasHold();if(screensaverActive){if(ac||ah||bc||bh)leaveSaver();return;}if(ac||ah||bc||bh)lastInteractionAt=millis();if(!inApp){if(ac){selected=(selected+1)%APP_COUNT;renderMenu();}if(bc)enterSelected();return;}
  if(current==AppId::Settings&&bh){startSetupPortal();return;}
  if(bc){leaveApp();return;}
  if(current==AppId::OneTap){if(ah){nextOneTapMode();return;}if(ac){triggerOneTap();return;}}
  if(current==AppId::Voice){if(ah){recordVoiceLoopback();renderVoice();}return;}
  if(current==AppId::Level){if(ac){calibrateLevel();M5.Speaker.tone(2200,40);renderLevel();}return;}
  if(current==AppId::Market){if(ah){refreshMarket();return;}if(ac){pageIndex=(pageIndex+1)%marketPageCount();renderMarket();}return;}
  if(current==AppId::Info){if(ac){pageIndex=(pageIndex+1)%2;renderInfo();}return;}
  if(current==AppId::Schedule){if(ac)renderSchedule();return;}
  if(current==AppId::IR){if(ah){irHasFrame=irFrameValid=irRepeatFrame=false;renderIR();return;}if(ac){pageIndex=(pageIndex+1)%2;renderIR();}return;}
  if(current==AppId::Tilt){if(ac){gameX=W()/2.0f;gameY=H()/2.0f;gameVx=gameVy=0;renderTilt();}return;}
  if(current==AppId::Settings){if(ah&&pageIndex==1){activateSaver();return;}if(ac){pageIndex=(pageIndex+1)%2;renderSettings();}return;}
}

void serviceOneTapLive(){if(current!=AppId::OneTap)return;uint32_t now=millis();if(oneTapMode==OneTapMode::Challenge&&challengeActive){uint32_t elapsed=now-challengeStartedAt;if(elapsed>=30000){challengeActive=false;oneTapMain="完成！";oneTapDetail=challengeName+" · 30秒挑战完成";if(!challengeFinishedNotified){challengeFinishedNotified=true;M5.Speaker.tone(2400,120);}renderOneTap();return;}if(now-lastRender>=250){uint32_t remain=30-(elapsed/1000);oneTapMain=String(remain)+"秒";oneTapDetail=challengeName+" · 坚持到结束";renderOneTap();return;}}
  if(oneTapMode==OneTapMode::Reaction&&reactionState==ReactionState::Waiting&&(int32_t)(now-reactionGoAt)>=0){reactionState=ReactionState::Ready;reactionReadyAt=reactionGoAt;oneTapMain="按！";oneTapDetail="现在按 A";M5.Speaker.tone(2300,30);renderOneTap();}
}
void periodicRefresh(){if(setupPortalActive)return;uint32_t now=millis();if(!screensaverActive&&!irActive&&now-lastInteractionAt>=SCREENSAVER_TIMEOUT_MS){activateSaver();return;}if(screensaverActive||!inApp)return;if(current==AppId::OneTap){serviceOneTapLive();return;}if(current==AppId::IR){if(pollIr())renderIR();return;}if(current==AppId::Tilt&&now-lastRender>=45){renderTilt();return;}if(current==AppId::Level&&now-lastRender>=90){renderLevel();return;}if((current==AppId::Info||current==AppId::Schedule||current==AppId::Settings)&&now-lastSecond>=1000){lastSecond=now;renderCurrent();}}
void serviceSetupPortal(){if(!setupPortalActive)return;setupServer.handleClient();if(M5.BtnB.wasClicked()||millis()-setupStartedAt>=SETUP_TIMEOUT_MS){stopSetupPortal();lastInteractionAt=millis();renderMenu();}}

} // namespace pn

void setup(){auto cfg=M5.config();M5.begin(cfg);Serial.begin(115200);M5.Display.setRotation(0);M5.Display.setTextWrap(false);pn::frame.setColorDepth(16);pn::frame.createSprite(M5.Display.width(),M5.Display.height());setenv("TZ",PN_TIMEZONE,1);tzset();pn::beginFrame();pn::drawStatusBar();pn::fontUI();pn::frame.setTextColor(pn::ACCENT,pn::BG);pn::frame.drawString("Pocket Nexus",8,78);pn::fontSmall();pn::frame.setTextColor(pn::MUTED,pn::BG);pn::frame.drawString(String("版本 ")+pn::VERSION,8,115);pn::frame.drawString("一键乐扩展 · RSS · 挑战",8,140);pn::present();delay(350);pn::lastInteractionAt=millis();pn::connectWiFi();if(!pn::setupPortalActive)pn::renderMenu();}
void loop(){M5.update();pn::serviceWiFi();pn::serviceSetupPortal();if(!pn::setupPortalActive){pn::handleButtons();pn::periodicRefresh();}delay(5);}
