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


replace_once('constexpr const char* VERSION = "0.8";', 'constexpr const char* VERSION = "0.9";', 'version')

replace_once(
    '  Meal, RandomRss, Challenge, Reaction, DecisionWheel, Count\n};',
    '  Meal, RandomRss, Challenge, Reaction, DecisionWheel, Rps, Pomodoro, PoseTimer, SoundMeter, WifiScan, Count\n};',
    'one tap enum',
)

replace_once(
    'ReactionState reactionState=ReactionState::Idle;\nuint32_t reactionGoAt=0, reactionReadyAt=0;\n',
    '''ReactionState reactionState=ReactionState::Idle;\nuint32_t reactionGoAt=0, reactionReadyAt=0;\n\n// v0.9 Hackster-inspired tools: focus timer, pose timer, mic meter and passive Wi-Fi scan.\nbool pomoRunning=false, pomoPaused=false, pomoBreakPhase=false;\nuint32_t pomoEndAt=0, pomoRemainingMs=25UL*60UL*1000UL;\nbool poseTimerRunning=false;\nuint32_t poseTimerEndAt=0;\nint poseTimerMinutes=15;\n''',
    'v09 state',
)

replace_once(
    'case OneTapMode::DecisionWheel:return "决策轮盘";default:return "一键乐";',
    'case OneTapMode::DecisionWheel:return "决策轮盘";case OneTapMode::Rps:return "猜拳";case OneTapMode::Pomodoro:return "专注计时";case OneTapMode::PoseTimer:return "姿态计时";case OneTapMode::SoundMeter:return "声音仪";case OneTapMode::WifiScan:return "附近 Wi-Fi";default:return "一键乐";',
    'mode names',
)

helper_code = r'''
String formatMs(uint32_t ms){uint32_t total=ms/1000;uint32_t m=total/60,s=total%60;char b[12];snprintf(b,sizeof(b),"%02lu:%02lu",(unsigned long)m,(unsigned long)s);return String(b);}
void syncPomodoroDisplay(){uint32_t remain=pomoRunning?((int32_t)(pomoEndAt-millis())>0?pomoEndAt-millis():0):pomoRemainingMs;oneTapMain=formatMs(remain);oneTapDetail=String(pomoBreakPhase?"休息":"专注")+(pomoRunning?" · A 暂停":(pomoPaused?" · 已暂停 · A 继续":" · A 开始"));}
void handlePomodoroTap(){uint32_t now=millis();if(pomoRunning){pomoRemainingMs=(int32_t)(pomoEndAt-now)>0?pomoEndAt-now:0;pomoRunning=false;pomoPaused=true;syncPomodoroDisplay();return;}if(pomoRemainingMs==0)pomoRemainingMs=(pomoBreakPhase?5UL:25UL)*60UL*1000UL;pomoEndAt=now+pomoRemainingMs;pomoRunning=true;pomoPaused=false;syncPomodoroDisplay();}
int currentPoseMinutes(){Vec3 g=readAccel();if(g.x<-0.55f)return 5;if(g.y<-0.55f)return 10;if(g.x>0.55f)return 25;if(g.y>0.55f)return 45;return 15;}
void syncPoseTimerDisplay(){if(poseTimerRunning){uint32_t remain=(int32_t)(poseTimerEndAt-millis())>0?poseTimerEndAt-millis():0;oneTapMain=formatMs(remain);oneTapDetail=String(poseTimerMinutes)+" 分钟姿态计时 · A 取消";}else{poseTimerMinutes=currentPoseMinutes();oneTapMain=String(poseTimerMinutes)+" 分钟";oneTapDetail="倾斜设备改变 5/10/15/25/45 分钟 · A 开始";}}
void handlePoseTimerTap(){if(poseTimerRunning){poseTimerRunning=false;syncPoseTimerDisplay();return;}poseTimerMinutes=currentPoseMinutes();poseTimerEndAt=millis()+(uint32_t)poseTimerMinutes*60UL*1000UL;poseTimerRunning=true;syncPoseTimerDisplay();}
void measureSound(){static int16_t samples[1024];M5.Speaker.end();M5.Mic.begin();size_t pos=0;while(pos<1024){size_t n=min((size_t)256,(size_t)(1024-pos));if(M5.Mic.record(samples+pos,n,16000))pos+=n;M5.update();delay(1);}while(M5.Mic.isRecording())delay(1);M5.Mic.end();M5.Speaker.begin();double sum=0;int peak=0;for(size_t i=0;i<1024;++i){int v=abs((int)samples[i]);sum+=(double)v*v;if(v>peak)peak=v;}double rms=sqrt(sum/1024.0);double dbfs=20.0*log10(max(rms,1.0)/32768.0);int level=constrain((int)round((dbfs+60.0)*100.0/60.0),0,100);oneTapMain=String(level)+" /100";oneTapDetail=String("峰值 ")+String((int)round(peak*100.0/32768.0))+"% · 相对声级（未校准 dBA）";}
String wifiSecurityLabel(wifi_auth_mode_t auth){return auth==WIFI_AUTH_OPEN?"开放":"加密";}
void scanNearbyWifi(){oneTapMain="扫描中…";oneTapDetail="被动查看附近网络，不发送攻击帧";renderOneTap();int n=WiFi.scanNetworks(false,false);if(n<=0){oneTapMain=n==0?"未发现网络":"扫描失败";oneTapDetail="再按 A 重试";WiFi.scanDelete();return;}int best=0;for(int i=1;i<n;++i)if(WiFi.RSSI(i)>WiFi.RSSI(best))best=i;String name=WiFi.SSID(best);if(!name.length())name="（隐藏网络）";oneTapMain=name;oneTapDetail=String(n)+" 个网络 · "+String(WiFi.RSSI(best))+" dBm · CH "+String(WiFi.channel(best))+" · "+wifiSecurityLabel(WiFi.encryptionType(best));WiFi.scanDelete();}
void playRps(){static const char* hands[]={"石头","剪刀","布"};oneTapMain=hands[esp_random()%3];oneTapDetail="先在心里出拳，再按 A 看设备出什么";}
void serviceBackgroundTimers(){uint32_t now=millis();if(pomoRunning&&(int32_t)(now-pomoEndAt)>=0){pomoRunning=false;pomoPaused=false;pomoBreakPhase=!pomoBreakPhase;pomoRemainingMs=(pomoBreakPhase?5UL:25UL)*60UL*1000UL;M5.Speaker.tone(pomoBreakPhase?2200:2500,180);if(inApp&&current==AppId::OneTap&&oneTapMode==OneTapMode::Pomodoro){syncPomodoroDisplay();renderOneTap();}}if(poseTimerRunning&&(int32_t)(now-poseTimerEndAt)>=0){poseTimerRunning=false;M5.Speaker.tone(2450,180);if(inApp&&current==AppId::OneTap&&oneTapMode==OneTapMode::PoseTimer){oneTapMain="时间到！";oneTapDetail=String(poseTimerMinutes)+" 分钟完成 · A 重新开始";renderOneTap();}}}
'''

replace_once('void triggerOneTap(){\n', helper_code + '\nvoid triggerOneTap(){\n', 'helper insertion')

replace_once(
    '    case OneTapMode::DecisionWheel:{oneTapMain=pickDelimitedOption(decisionOptions);oneTapDetail="候选项可在设置页修改";break;}\n    default:break;',
    '''    case OneTapMode::DecisionWheel:{oneTapMain=pickDelimitedOption(decisionOptions);oneTapDetail="候选项可在设置页修改";break;}\n    case OneTapMode::Rps:{playRps();break;}\n    case OneTapMode::Pomodoro:{handlePomodoroTap();break;}\n    case OneTapMode::PoseTimer:{handlePoseTimerTap();break;}\n    case OneTapMode::SoundMeter:{measureSound();break;}\n    case OneTapMode::WifiScan:{scanNearbyWifi();break;}\n    default:break;''',
    'trigger cases',
)

replace_once(
    '  if(oneTapMode==OneTapMode::Reaction&&reactionState==ReactionState::Waiting&&(int32_t)(now-reactionGoAt)>=0){reactionState=ReactionState::Ready;reactionReadyAt=reactionGoAt;oneTapMain="按！";oneTapDetail="现在按 A";M5.Speaker.tone(2300,30);renderOneTap();}\n}',
    '''  if(oneTapMode==OneTapMode::Reaction&&reactionState==ReactionState::Waiting&&(int32_t)(now-reactionGoAt)>=0){reactionState=ReactionState::Ready;reactionReadyAt=reactionGoAt;oneTapMain="按！";oneTapDetail="现在按 A";M5.Speaker.tone(2300,30);renderOneTap();}\n  if(oneTapMode==OneTapMode::Pomodoro&&now-lastRender>=500){syncPomodoroDisplay();renderOneTap();return;}\n  if(oneTapMode==OneTapMode::PoseTimer&&now-lastRender>=250){syncPoseTimerDisplay();renderOneTap();return;}\n}',
    'live services',
)

replace_once(
    'void periodicRefresh(){if(setupPortalActive)return;uint32_t now=millis();if(!screensaverActive&&!irActive&&now-lastInteractionAt>=SCREENSAVER_TIMEOUT_MS){activateSaver();return;}',
    'void periodicRefresh(){if(setupPortalActive)return;uint32_t now=millis();serviceBackgroundTimers();if(!screensaverActive&&!irActive&&now-lastInteractionAt>=SCREENSAVER_TIMEOUT_MS){activateSaver();return;}',
    'background timers',
)

replace_once(
    'pn::frame.drawString("一键乐扩展 · RSS · 挑战",8,140);',
    'pn::frame.drawString("Hackster 灵感 · 口袋工具",8,140);',
    'boot subtitle',
)

out_path.write_text(s, encoding="utf-8")
print(f"Generated {out_path} from {src_path}: {len(s)} bytes")
