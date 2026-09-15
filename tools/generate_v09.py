from pathlib import Path

src_path = Path("src/main_v08.cpp")
out_path = Path("src/main_v09.cpp")
s = src_path.read_text(encoding="utf-8")


def replace_once(old: str, new: str, label: str):
    global s
    count = s.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly 1 match, got {count}")
    s = s.replace(old, new, 1)


replace_once(
    'constexpr const char* VERSION = "0.8";',
    'constexpr const char* VERSION = "0.9";',
    'version',
)

replace_once(
    '  Meal, RandomRss, Challenge, Reaction, DecisionWheel, Count\n};',
    '  Meal, RandomRss, Challenge, Reaction, DecisionWheel, Rps, Pomodoro, PoseTimer, SoundMeter, WifiScan, Count\n};',
    'one tap enum',
)

replace_once(
    'ReactionState reactionState=ReactionState::Idle;\nuint32_t reactionGoAt=0, reactionReadyAt=0;\n',
    'ReactionState reactionState=ReactionState::Idle;\nuint32_t reactionGoAt=0, reactionReadyAt=0;\nbool oneTapMenu=true;\n\n// v0.9 Hackster-inspired tools.\nbool pomoRunning=false, pomoPaused=false, pomoBreakPhase=false;\nuint32_t pomoEndAt=0, pomoRemainingMs=25UL*60UL*1000UL;\nbool poseTimerRunning=false;\nuint32_t poseTimerEndAt=0;\nint poseTimerMinutes=15;\n',
    'v09 state',
)

replace_once(
    'case OneTapMode::DecisionWheel:return "决策轮盘";default:return "一键乐";',
    'case OneTapMode::DecisionWheel:return "决策轮盘";case OneTapMode::Rps:return "猜拳";case OneTapMode::Pomodoro:return "专注计时";case OneTapMode::PoseTimer:return "姿态计时";case OneTapMode::SoundMeter:return "声音仪";case OneTapMode::WifiScan:return "附近 Wi-Fi";default:return "一键乐";',
    'mode names',
)

old_menu_block = '''void renderOneTap(){beginFrame();drawAppHeader("一键乐");fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(oneTapModeName(oneTapMode),8,70);fontBig();frame.setTextColor(ACCENT,BG);drawWrappedText(oneTapMain,8,94,W()-16,30,3,ACCENT);fontSmall();frame.setTextColor(FG,BG);drawWrappedText(oneTapDetail,8,164,W()-16,15,3,FG);drawFooter("A 触发","长A 换玩法");present();}
void resetOneTapTransient(){challengeActive=false;challengeFinishedNotified=false;reactionState=ReactionState::Idle;reactionGoAt=reactionReadyAt=0;}
void nextOneTapMode(){resetOneTapTransient();uint8_t n=(static_cast<uint8_t>(oneTapMode)+1)%static_cast<uint8_t>(OneTapMode::Count);oneTapMode=(OneTapMode)n;oneTapMain="按 A";oneTapDetail="短按A触发 · 长按A换玩法";renderOneTap();}
'''

new_menu_block = '''void renderOneTap(){beginFrame();drawAppHeader("一键乐");fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(oneTapModeName(oneTapMode),8,70);fontBig();frame.setTextColor(ACCENT,BG);drawWrappedText(oneTapMain,8,94,W()-16,30,3,ACCENT);fontSmall();frame.setTextColor(FG,BG);drawWrappedText(oneTapDetail,8,164,W()-16,15,3,FG);drawFooter("A 触发","B 菜单");present();}
void renderOneTapMenu(){beginFrame();drawAppHeader("一键乐");uint8_t idx=static_cast<uint8_t>(oneTapMode),count=static_cast<uint8_t>(OneTapMode::Count);fontSmall();frame.setTextColor(MUTED,BG);frame.drawString(String("玩法 ")+String(idx+1)+"/"+String(count),8,66);constexpr uint8_t rows=4;uint8_t start=idx>=rows?idx-rows+1:0;for(uint8_t r=0;r<rows;++r){uint8_t item=start+r;if(item>=count)break;int y=84+r*29;bool active=item==idx;frame.fillRoundRect(6,y,W()-12,25,6,active?BLUE:PANEL2);if(!active)frame.drawRoundRect(6,y,W()-12,25,6,MUTED);fontSmall();frame.setTextColor(active?FG:MUTED,active?BLUE:PANEL2);frame.drawString(String(item+1),11,y+5);frame.setTextColor(FG,active?BLUE:PANEL2);frame.drawString(oneTapModeName((OneTapMode)item),31,y+5);}fontSmall();frame.setTextColor(MUTED,BG);frame.drawString("长按B返回主菜单",8,204);drawFooter("A 下一项","B 打开");present();}
void resetOneTapTransient(){challengeActive=false;challengeFinishedNotified=false;reactionState=ReactionState::Idle;reactionGoAt=reactionReadyAt=0;}
void selectNextOneTap(){resetOneTapTransient();uint8_t n=(static_cast<uint8_t>(oneTapMode)+1)%static_cast<uint8_t>(OneTapMode::Count);oneTapMode=(OneTapMode)n;renderOneTapMenu();}
'''
replace_once(old_menu_block, new_menu_block, 'one tap second-level menu')

helper_code = r'''
String formatMs(uint32_t ms){uint32_t total=ms/1000;uint32_t m=total/60,s=total%60;char b[12];snprintf(b,sizeof(b),"%02lu:%02lu",(unsigned long)m,(unsigned long)s);return String(b);}
void syncPomodoroDisplay(){uint32_t remain=pomoRunning?((int32_t)(pomoEndAt-millis())>0?pomoEndAt-millis():0):pomoRemainingMs;oneTapMain=formatMs(remain);oneTapDetail=String(pomoBreakPhase?"休息":"专注")+(pomoRunning?" · A 暂停":(pomoPaused?" · 已暂停 · A 继续":" · A 开始"));}
void handlePomodoroTap(){uint32_t now=millis();if(pomoRunning){pomoRemainingMs=(int32_t)(pomoEndAt-now)>0?pomoEndAt-now:0;pomoRunning=false;pomoPaused=true;syncPomodoroDisplay();return;}if(pomoRemainingMs==0)pomoRemainingMs=(pomoBreakPhase?5UL:25UL)*60UL*1000UL;pomoEndAt=now+pomoRemainingMs;pomoRunning=true;pomoPaused=false;syncPomodoroDisplay();}
int currentPoseMinutes(){Vec3 g=readAccel();if(g.x<-0.55f)return 5;if(g.y<-0.55f)return 10;if(g.x>0.55f)return 25;if(g.y>0.55f)return 45;return 15;}
void syncPoseTimerDisplay(){if(poseTimerRunning){uint32_t remain=(int32_t)(poseTimerEndAt-millis())>0?poseTimerEndAt-millis():0;oneTapMain=formatMs(remain);oneTapDetail=String(poseTimerMinutes)+" 分钟姿态计时 · A 取消";}else{poseTimerMinutes=currentPoseMinutes();oneTapMain=String(poseTimerMinutes)+" 分钟";oneTapDetail="倾斜设备改变 5/10/15/25/45 分钟 · A 开始";}}
void handlePoseTimerTap(){if(poseTimerRunning){poseTimerRunning=false;syncPoseTimerDisplay();return;}poseTimerMinutes=currentPoseMinutes();poseTimerEndAt=millis()+(uint32_t)poseTimerMinutes*60UL*1000UL;poseTimerRunning=true;syncPoseTimerDisplay();}
void measureSound(){static int16_t samples[1024];M5.Speaker.end();M5.Mic.begin();size_t pos=0;while(pos<1024){size_t n=min((size_t)256,(size_t)(1024-pos));if(M5.Mic.record(samples+pos,n,16000))pos+=n;M5.update();delay(1);}while(M5.Mic.isRecording())delay(1);M5.Mic.end();M5.Speaker.begin();double sum=0;int peak=0;for(size_t i=0;i<1024;++i){int v=abs((int)samples[i]);sum+=(double)v*v;if(v>peak)peak=v;}double rms=sqrt(sum/1024.0);double dbfs=20.0*log10((rms>1.0?rms:1.0)/32768.0);int level=constrain((int)round((dbfs+60.0)*100.0/60.0),0,100);oneTapMain=String(level)+" /100";oneTapDetail=String("峰值 ")+String((int)round(peak*100.0/32768.0))+"% · 相对声级（未校准 dBA）";}
String wifiSecurityLabel(wifi_auth_mode_t auth){return auth==WIFI_AUTH_OPEN?"开放":"加密";}
void scanNearbyWifi(){oneTapMain="扫描中…";oneTapDetail="被动查看附近网络";renderOneTap();int n=WiFi.scanNetworks(false,false);if(n<=0){oneTapMain=n==0?"未发现网络":"扫描失败";oneTapDetail="再按 A 重试";WiFi.scanDelete();return;}int best=0;for(int i=1;i<n;++i)if(WiFi.RSSI(i)>WiFi.RSSI(best))best=i;String name=WiFi.SSID(best);if(!name.length())name="（隐藏网络）";oneTapMain=name;oneTapDetail=String(n)+" 个网络 · "+String(WiFi.RSSI(best))+" dBm · CH "+String(WiFi.channel(best))+" · "+wifiSecurityLabel(WiFi.encryptionType(best));WiFi.scanDelete();}
void playRps(){static const char* hands[]={"石头","剪刀","布"};oneTapMain=hands[esp_random()%3];oneTapDetail="先在心里出拳，再按 A 看设备出什么";}
void prepareOneTapTool(){resetOneTapTransient();if(oneTapMode==OneTapMode::Pomodoro)syncPomodoroDisplay();else if(oneTapMode==OneTapMode::PoseTimer)syncPoseTimerDisplay();else{oneTapMain="按 A";oneTapDetail="按 A 触发当前玩法";}renderOneTap();}
void serviceBackgroundTimers(){uint32_t now=millis();if(pomoRunning&&(int32_t)(now-pomoEndAt)>=0){pomoRunning=false;pomoPaused=false;pomoBreakPhase=!pomoBreakPhase;pomoRemainingMs=(pomoBreakPhase?5UL:25UL)*60UL*1000UL;M5.Speaker.tone(pomoBreakPhase?2200:2500,180);if(inApp&&current==AppId::OneTap&&!oneTapMenu&&oneTapMode==OneTapMode::Pomodoro){syncPomodoroDisplay();renderOneTap();}}if(poseTimerRunning&&(int32_t)(now-poseTimerEndAt)>=0){poseTimerRunning=false;M5.Speaker.tone(2450,180);if(inApp&&current==AppId::OneTap&&!oneTapMenu&&oneTapMode==OneTapMode::PoseTimer){oneTapMain="时间到！";oneTapDetail=String(poseTimerMinutes)+" 分钟完成 · A 重新开始";renderOneTap();}}}
'''
replace_once('void triggerOneTap(){\n', helper_code + '\nvoid triggerOneTap(){\n', 'helper insertion')

replace_once(
    '    case OneTapMode::DecisionWheel:{oneTapMain=pickDelimitedOption(decisionOptions);oneTapDetail="候选项可在设置页修改";break;}\n    default:break;',
    '    case OneTapMode::DecisionWheel:{oneTapMain=pickDelimitedOption(decisionOptions);oneTapDetail="候选项可在设置页修改";break;}\n    case OneTapMode::Rps:{playRps();break;}\n    case OneTapMode::Pomodoro:{handlePomodoroTap();break;}\n    case OneTapMode::PoseTimer:{handlePoseTimerTap();break;}\n    case OneTapMode::SoundMeter:{measureSound();break;}\n    case OneTapMode::WifiScan:{scanNearbyWifi();break;}\n    default:break;',
    'trigger cases',
)

replace_once(
    'void renderCurrent(){lastRender=millis();switch(current){case AppId::Info:renderInfo();break;case AppId::Market:renderMarket();break;case AppId::OneTap:renderOneTap();break;',
    'void renderCurrent(){lastRender=millis();switch(current){case AppId::Info:renderInfo();break;case AppId::Market:renderMarket();break;case AppId::OneTap:if(oneTapMenu)renderOneTapMenu();else renderOneTap();break;',
    'render one tap menu',
)

replace_once(
    'if(current==AppId::OneTap){resetOneTapTransient();oneTapMain="按 A";oneTapDetail="短按A触发 · 长按A换玩法";}renderCurrent();}',
    'if(current==AppId::OneTap){resetOneTapTransient();oneTapMenu=true;}renderCurrent();}',
    'enter one tap menu',
)

old_handle = '''void handleButtons(){bool ac=M5.BtnA.wasClicked(),ah=M5.BtnA.wasHold(),bc=M5.BtnB.wasClicked(),bh=M5.BtnB.wasHold();if(screensaverActive){if(ac||ah||bc||bh)leaveSaver();return;}if(ac||ah||bc||bh)lastInteractionAt=millis();if(!inApp){if(ac){selected=(selected+1)%APP_COUNT;renderMenu();}if(bc)enterSelected();return;}
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
'''

new_handle = '''void handleButtons(){bool ac=M5.BtnA.wasClicked(),ah=M5.BtnA.wasHold(),bc=M5.BtnB.wasClicked(),bh=M5.BtnB.wasHold();if(screensaverActive){if(ac||ah||bc||bh)leaveSaver();return;}if(ac||ah||bc||bh)lastInteractionAt=millis();if(!inApp){if(ac){selected=(selected+1)%APP_COUNT;renderMenu();}if(bc)enterSelected();return;}
  if(current==AppId::Settings&&bh){startSetupPortal();return;}
  if(current==AppId::OneTap){if(oneTapMenu){if(bh){leaveApp();return;}if(ac){selectNextOneTap();return;}if(bc){oneTapMenu=false;prepareOneTapTool();return;}return;}if(bc||bh){resetOneTapTransient();oneTapMenu=true;renderOneTapMenu();return;}if(ac||ah){triggerOneTap();return;}return;}
  if(bc){leaveApp();return;}
  if(current==AppId::Voice){if(ah){recordVoiceLoopback();renderVoice();}return;}
  if(current==AppId::Level){if(ac){calibrateLevel();M5.Speaker.tone(2200,40);renderLevel();}return;}
  if(current==AppId::Market){if(ah){refreshMarket();return;}if(ac){pageIndex=(pageIndex+1)%marketPageCount();renderMarket();}return;}
  if(current==AppId::Info){if(ac){pageIndex=(pageIndex+1)%2;renderInfo();}return;}
  if(current==AppId::Schedule){if(ac)renderSchedule();return;}
  if(current==AppId::IR){if(ah){irHasFrame=irFrameValid=irRepeatFrame=false;renderIR();return;}if(ac){pageIndex=(pageIndex+1)%2;renderIR();}return;}
  if(current==AppId::Tilt){if(ac){gameX=W()/2.0f;gameY=H()/2.0f;gameVx=gameVy=0;renderTilt();}return;}
  if(current==AppId::Settings){if(ah&&pageIndex==1){activateSaver();return;}if(ac){pageIndex=(pageIndex+1)%2;renderSettings();}return;}
}
'''
replace_once(old_handle, new_handle, 'button navigation')

replace_once(
    'void serviceOneTapLive(){if(current!=AppId::OneTap)return;',
    'void serviceOneTapLive(){if(current!=AppId::OneTap||oneTapMenu)return;',
    'one tap live menu guard',
)

replace_once(
    '  if(oneTapMode==OneTapMode::Reaction&&reactionState==ReactionState::Waiting&&(int32_t)(now-reactionGoAt)>=0){reactionState=ReactionState::Ready;reactionReadyAt=reactionGoAt;oneTapMain="按！";oneTapDetail="现在按 A";M5.Speaker.tone(2300,30);renderOneTap();}\n}',
    '  if(oneTapMode==OneTapMode::Reaction&&reactionState==ReactionState::Waiting&&(int32_t)(now-reactionGoAt)>=0){reactionState=ReactionState::Ready;reactionReadyAt=reactionGoAt;oneTapMain="按！";oneTapDetail="现在按 A";M5.Speaker.tone(2300,30);renderOneTap();}\n  if(oneTapMode==OneTapMode::Pomodoro&&now-lastRender>=500){syncPomodoroDisplay();renderOneTap();return;}\n  if(oneTapMode==OneTapMode::PoseTimer&&now-lastRender>=250){syncPoseTimerDisplay();renderOneTap();return;}\n}',
    'live tool updates',
)

replace_once(
    'void periodicRefresh(){if(setupPortalActive)return;uint32_t now=millis();if(!screensaverActive&&!irActive&&now-lastInteractionAt>=SCREENSAVER_TIMEOUT_MS){activateSaver();return;}',
    'void periodicRefresh(){if(setupPortalActive)return;uint32_t now=millis();serviceBackgroundTimers();if(!screensaverActive&&!irActive&&now-lastInteractionAt>=SCREENSAVER_TIMEOUT_MS){activateSaver();return;}',
    'background timers',
)

replace_once(
    'pn::frame.drawString("一键乐扩展 · RSS · 挑战",8,140);',
    'pn::frame.drawString("二级菜单 · Hackster 灵感",8,140);',
    'boot subtitle',
)

out_path.write_text(s, encoding="utf-8")
print(f"Generated {out_path} from {src_path}: {len(s)} bytes")
