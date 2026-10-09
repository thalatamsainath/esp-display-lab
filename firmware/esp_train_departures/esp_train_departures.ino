#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <memory>
#include <stddef.h>
#include "ota_credentials.h"
#include "rail_logic.h"
#include "rail_board.h"
#include "rtt_ca.h"
#include "rtt_board.h"
#include "rtt_policy.h"
#include "rail_config_json.h"
#include "web_pages.h"

constexpr uint16_t BG=0x0841, FG=0xF79E, MUTED=0xA596, TRACK=0x2947, PURPLE=0xBD1F, GREEN=0x8E56, AMBER=0xFE69, RED=0xFC30;
constexpr unsigned EEPROM_SIZE=4096, CONFIG_ADDR=128, BRIGHT_MARKER=96, BRIGHT_VALUE=97;
constexpr uint32_t CONFIG_MAGIC=RailConfig::MAGIC;
constexpr const char* RAIL_BASE="https://data.rtt.io";
constexpr const char* AP_SSID="MiniScreen-Setup";
constexpr const char* AP_PASS="12345678";
using Settings=RailConfig::Settings;
using Stored=RailConfig::Stored;
static_assert(CONFIG_ADDR+sizeof(Stored) <= EEPROM_SIZE,"EEPROM settings must fit");
TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;
Settings config = {};
RailBoard::Snapshot board = {};
RttBoard::Diagnostics railDiagnostics;int railHttpStatus=0;
bool hasData=false, apMode=false, fetchPending=true, brightnessPending=false;
bool mflnChecked=false, mflnSupported=false;
unsigned brightness=100, savedBrightness=100;
uint32_t brightnessChangedAt=0, lastFetch=0, lastRender=0;
String fetchError,accessToken;
time_t accessValidUntil=0;
uint32_t retryAt=0; bool retryPending=false,quotaBlocked=false;
String fields[7];
bool screenInitialized=false,attemptedFetch=false;
int scheduleIndex=-99;bool schedulePaused=false;
RailSchedule::Rule previousRule={};char boardRoute[8]={};
const RailSchedule::Rule& activeRule(time_t now) {return RailSchedule::rule(config.schedule,RailSchedule::at(config.schedule,now).index);}
bool boardMatches(time_t now) {const auto& r=activeRule(now);return hasData && board.arrivals==(r.mode==RailSchedule::ARRIVALS) && !strcmp(boardRoute,r.route);}


using RailConfig::bytesChecksum;
uint32_t checksum(const Settings& value) {return bytesChecksum(&value,sizeof(value));}
bool validOperators(const char* value) {return RailConfig::validOperators(value);}
uint32_t pollMs() {return uint32_t(activeRule(time(nullptr)).interval)*60000u;}
bool validSettings(const Settings& value) {return RailConfig::valid(value);}
void loadSettings() {
  config=RailConfig::defaults();
  EEPROM.begin(EEPROM_SIZE);uint32_t magic,sum;EEPROM.get(CONFIG_ADDR,magic);
  if(magic==CONFIG_MAGIC) {
    EEPROM.get(CONFIG_ADDR+offsetof(Stored,settings),config);
    EEPROM.get(CONFIG_ADDR+offsetof(Stored,checksum),sum);
    if(sum!=checksum(config) || !validSettings(config))config=RailConfig::defaults();
  } else if(magic==0x54524E32) {
    std::unique_ptr<RailConfig::LegacyStoredV2> old(new(std::nothrow) RailConfig::LegacyStoredV2{});
    if(old) {
      EEPROM.get(CONFIG_ADDR,*old);
      if(old->checksum==bytesChecksum(&old->settings,sizeof(old->settings)))RailConfig::migrate(old->settings,config);
    }
  } else if(magic==0x54524E31) {
    struct LegacySettings {char railUser[65],railPassword[161],station[4],direction[8],operators[10];uint8_t demo;};
    struct LegacyStored {uint32_t magic;LegacySettings settings;uint32_t checksum;};
    LegacyStored old;EEPROM.get(CONFIG_ADDR,old);
    if(old.checksum==bytesChecksum(&old.settings,sizeof(old.settings)) && old.settings.station[3]==0 && old.settings.direction[7]==0 &&
       old.settings.operators[9]==0 && old.settings.demo<=1 && RailLogic::crs(old.settings.station) &&
       RailLogic::directionValid(old.settings.station,old.settings.direction) && validOperators(old.settings.operators)) {
      strcpy(config.station,old.settings.station);strcpy(config.schedule.fallback.route,old.settings.direction);
      strcpy(config.operators,old.settings.operators);config.demo=old.settings.demo;
    }
  }
  unsigned value=EEPROM.read(BRIGHT_VALUE);
  brightness=EEPROM.read(BRIGHT_MARKER)==0xB4 && value<=100?value:100;
  savedBrightness=brightness;EEPROM.end();
}
bool persistSettings(const Settings& next) {
  // Avoid a second large Settings copy on the ESP8266's small stack.
  uint32_t magic=CONFIG_MAGIC,sum=checksum(next);
  EEPROM.begin(EEPROM_SIZE);EEPROM.put(CONFIG_ADDR,magic);
  EEPROM.put(CONFIG_ADDR+offsetof(Stored,settings),next);EEPROM.put(CONFIG_ADDR+offsetof(Stored,checksum),sum);
  bool ok=EEPROM.commit();EEPROM.end();return ok;
}
void applyBrightness(unsigned value) { brightness=value; analogWrite(TFT_BL,RailLogic::backlightDuty(value)); }
void persistBrightness() {
  if(!brightnessPending || millis()-brightnessChangedAt<1200) return;
  brightnessPending=false; if(savedBrightness==brightness) return;
  EEPROM.begin(EEPROM_SIZE); EEPROM.write(BRIGHT_MARKER,0xB4); EEPROM.write(BRIGHT_VALUE,brightness);
  if(EEPROM.commit()) savedBrightness=brightness;
  else { brightnessPending=true; brightnessChangedAt=millis(); }
  EEPROM.end();
}
String quote(const String& value) {
  String out="\"";
  for(unsigned i=0;i<value.length();++i) { unsigned char c=value[i]; if(c=='"' || c=='\\') out+='\\'; if(c>=32) out+=char(c); else out+=' '; }
  return out+'"';
}
String minutesLabel(unsigned minute) {char value[6];snprintf(value,sizeof(value),"%02u:%02u",minute/60,minute%60);return value;}
bool parseMinutes(const String& label,unsigned& minute) {
  int hour,part;if(!RailLogic::clock(label.c_str(),hour,part))return false;minute=hour*60+part;return true;
}
String ruleJson(const RailSchedule::Rule& rule,bool window) {
  String out="{\"mode\":"+quote(RailSchedule::modeName(rule.mode))+",\"intervalMinutes\":"+String(rule.interval)+",\"direction\":"+quote(rule.route);
  if(window)out+=",\"start\":"+quote(minutesLabel(rule.start))+",\"end\":"+quote(minutesLabel(rule.end));
  return out+"}";
}
String settingsJson() {
  String out="{\"station\":"+quote(config.station)+",\"operators\":"+quote(config.operators)+",\"hasToken\":"+(config.refreshToken[0]?"true":"false")+",\"demo\":"+(config.demo?"true":"false")+",\"railBase\":"+quote(RAIL_BASE)+",\"schedule\":{\"start\":"+quote(minutesLabel(config.schedule.start))+",\"end\":"+quote(minutesLabel(config.schedule.end))+",\"allDay\":"+(config.schedule.allDay?"true":"false")+",\"default\":"+ruleJson(config.schedule.fallback,false)+",\"rules\":[";
  for(unsigned i=0;i<config.schedule.count;++i) {if(i)out+=',';out+=ruleJson(config.schedule.rules[i],true);}
  return out+"]},\"dailyRequests\":"+String(RailSchedule::dailyRequests(config.schedule))+"}";
}
String directionsJson(const char* station) {
  String out="[{\"id\":\"both\",\"label\":\"Both directions / all departures\"}";
  int pos=RailLogic::position(station);
  if(pos>=0) {
    if(pos<int(RailLogic::STATION_COUNT-1)) out+=",{\"id\":\"east\",\"label\":"+quote(pos<RailLogic::position("PAD")?"Towards London":"Towards Abbey Wood / Shenfield")+"}";
    if(pos>0) out+=",{\"id\":\"west\",\"label\":"+quote(pos<RailLogic::position("RDG")?"Towards Didcot Parkway":pos==RailLogic::position("RDG")?"Westbound / away from London":pos<RailLogic::position("HAY")?"Towards Reading":"Towards Reading / Heathrow")+"}";
    out+=",{\"id\":\"other\",\"label\":\"Other routes / branches\"}";
  }
  return out+"]";
}
void sendStations() {
  // A complete catalog plus String growth/copies can exceed the largest free
  // heap block after TLS. Stream small chunks instead of allocating 7+ KiB.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"application/json","");
  server.sendContent("{\"stations\":[");
  for(unsigned i=0;i<RailLogic::STATION_COUNT;++i) {
    if(i)server.sendContent(",");
    server.sendContent("{\"crs\":"+quote(RailLogic::stations[i].crs)+",\"name\":"+quote(RailLogic::stations[i].name)+",\"directions\":"+directionsJson(RailLogic::stations[i].crs)+"}");
  }
  server.sendContent("]}");
  server.sendContent("");
}
const char* statusName(RailLogic::Status status) {
  return status==RailLogic::CANCELLED?"cancelled":status==RailLogic::DELAYED?"delayed":status==RailLogic::ON_TIME?"on_time":"unknown";
}
bool quietHours(time_t now) {
  if(now<1700000000)return false;
  return RailSchedule::at(config.schedule,now).paused;
}
String activeError() {
  if(quietHours(time(nullptr)))return "";
  if(WiFi.status()!=WL_CONNECTED) return "Wi-Fi disconnected";
  if(fetchError.length()) return fetchError;
  if(hasData && RailLogic::stale(time(nullptr),board.generated,pollMs()/1000+30)) return "Rail data is stale - waiting for an update";
  return "";
}
String stateJson() {
  String error=activeError(); time_t now=time(nullptr); bool first=true;const auto& rule=activeRule(now);
  String out="{\"firmware\":\"train-schedule-rtt-1.8\",\"brightness\":"+String(brightness)+",\"hasData\":"+(hasData?"true":"false")+",\"demo\":"+(config.demo?"true":"false")+",\"error\":"+quote(error)+",\"ageSeconds\":"+(hasData?String(max(time_t(0),now-board.generated)):"null")+",\"quietHours\":"+(quietHours(now)?"true":"false")+",\"resumeAt\":"+quote(minutesLabel(config.schedule.start));
  out+=",\"mode\":"+quote(RailSchedule::modeName(rule.mode))+",\"intervalMinutes\":"+String(rule.interval)+",\"direction\":"+quote(rule.route)+",\"ruleIndex\":"+String(RailSchedule::at(config.schedule,now).index)+",\"trains\":[";
  // Suppress old station data and stale predictions rather than publishing a
  // plausible-looking countdown after a feed failure.
  if(boardMatches(now) && board.available && !quietHours(now) && !RailLogic::stale(now,board.generated,pollMs()/1000+30)) for(unsigned i=0;i<board.count;++i) {
    const auto& train=board.trains[i];
    if(!RailBoard::rowUpcoming(train,now))continue;
    if(!first)out+=','; first=false;
    out+="{\"destination\":"+quote(train.destination)+",\"operator\":"+quote(train.operatorName)+",\"scheduled\":"+quote(train.scheduled)+",\"expected\":"+quote(train.expected)+",\"platform\":"+quote(train.platform)+",\"status\":"+quote(statusName(train.forecast.status))+",\"delay\":"+String(train.forecast.delay)+",\"reason\":"+quote(train.reason)+"}";
  }
  return out+"]}";
}
String diagnosticsJson() {
  const auto& d=railDiagnostics;
  return "{\"httpStatus\":"+String(railHttpStatus)+",\"servicesSeen\":"+String(d.seen)+",\"kept\":"+String(board.count)+
    ",\"nonPassenger\":"+String(d.nonPassenger)+",\"operatorFiltered\":"+String(d.operatorFiltered)+",\"directionFiltered\":"+String(d.directionFiltered)+
    ",\"nonCalling\":"+String(d.nonCalling)+",\"invalidTime\":"+String(d.invalidTime)+",\"departed\":"+String(d.departed)+",\"expired\":"+String(d.expired)+
    ",\"freeHeap\":"+String(ESP.getFreeHeap())+",\"maxFreeBlock\":"+String(ESP.getMaxFreeBlockSize())+",\"clock\":"+String(time(nullptr))+",\"sample\":"+quote(d.sample)+"}";
}
void handleSettings() {
  String station=server.arg("station"),operators=server.arg("operators"),token=server.arg("refreshToken");
  station.trim();station.toUpperCase();token.trim();bool demo=server.arg("demo")=="1";
  if(!RailLogic::crs(station.c_str()) || !validOperators(operators.c_str()) || token.length()>2048 ||
     (token.length() && !RttPolicy::validToken(token.c_str())) || server.arg("railBase")!=RAIL_BASE || (!demo && !token.length() && !config.refreshToken[0])) {
    server.send(400,"application/json","{\"error\":\"Check station, operators and RTT refresh token\"}");return;
  }
  String payload=server.arg("schedule");
  if(!payload.length() || payload.length()>4096) {server.send(400,"application/json","{\"error\":\"Invalid schedule; reload the web UI\"}");return;}
  std::unique_ptr<Settings> next(new(std::nothrow) Settings(config));
  if(!next) {server.send(503,"application/json","{\"error\":\"Insufficient memory to save settings\"}");return;}
  DynamicJsonDocument doc(4096);const char* error=nullptr;
  if(deserializeJson(doc,payload) || !RailConfigJson::readSchedule(doc.as<JsonObjectConst>(),station.c_str(),next->schedule,error)) {
    server.send(400,"application/json","{\"error\":"+quote(error?error:"Invalid schedule JSON")+"}");return;
  }
  RailBoard::copy(next->station,sizeof(next->station),station.c_str());RailBoard::copy(next->operators,sizeof(next->operators),operators.c_str());
  if(token.length()) {memset(next->refreshToken,0,sizeof(next->refreshToken));memcpy(next->refreshToken,token.c_str(),token.length());}
  next->demo=demo;
  if(!persistSettings(*next)) {server.send(500,"application/json","{\"error\":\"Could not save settings\"}");return;}
  if(strcmp(config.refreshToken,next->refreshToken)) {accessToken="";accessValidUntil=0;}
  config=*next;hasData=false;board={};fetchError="Settings saved; waiting for the next permitted refresh";fetchPending=true;scheduleIndex=-99;
  server.send(200,"application/json","{\"ok\":true}");
}
void reconcileSchedule() {
  auto selection=RailSchedule::at(config.schedule,time(nullptr));const auto& rule=RailSchedule::rule(config.schedule,selection.index);
  if(scheduleIndex==selection.index && schedulePaused==selection.paused && RailSchedule::sameBoard(previousRule,rule) && previousRule.interval==rule.interval)return;
  if(selection.paused || schedulePaused || !RailSchedule::sameBoard(previousRule,rule)) {hasData=false;board={};fetchError="";}
  if(!selection.paused)fetchPending=true;
  scheduleIndex=selection.index;schedulePaused=selection.paused;previousRule=rule;
}

// Bound input length even when Content-Length is absent; ArduinoJson's filtered
// document is also bounded. The HTTPS client still verifies the root and host.
class LimitedStream : public Stream {
  Stream& input; size_t remaining;
public:
  LimitedStream(Stream& stream,size_t limit):input(stream),remaining(limit) { setTimeout(8000); }
  int available() override { return remaining ? input.available() : 0; }
  int read() override { if(!remaining) return -1; int value=input.read(); if(value>=0) --remaining; return value; }
  int peek() override { return remaining?input.peek():-1; }
  size_t write(uint8_t) override { return 0; }
  void flush() override {}
};
void makeDemo() {
  static const char json[] PROGMEM=R"JSON({"crs":"PAD","locationName":"London Paddington","areServicesAvailable":true,"platformAvailable":true,"trainServices":[{"std":"10:46","etd":"10:53","operatorCode":"GW","destination":[{"crs":"RDG","locationName":"Reading"}],"origin":[{"crs":"RDG"}],"platform":"4","delayReason":"Late incoming train (demo)"},{"std":"10:51","etd":"On time","operatorCode":"XR","destination":[{"crs":"ABW","locationName":"Abbey Wood"}],"origin":[{"crs":"RDG"}],"platform":"4"},{"std":"11:03","etd":"Cancelled","isCancelled":true,"operatorCode":"GW","destination":[{"crs":"RDG","locationName":"Reading"}],"origin":[{"crs":"ABW"}],"cancelReason":"Signalling fault (demo)"},{"std":"11:18","etd":"On time","operatorCode":"XR","destination":[{"crs":"RDG","locationName":"Reading"}],"origin":[{"crs":"ABW"}],"platform":"3"}]})JSON";
  DynamicJsonDocument doc(4096); deserializeJson(doc,FPSTR(json));
  time_t now=time(nullptr); struct tm local; localtime_r(&now,&local);
  struct tm utc; gmtime_r(&now,&utc); char stamp[25]; strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",&utc);
  doc["generatedAt"]=stamp; doc["crs"]=config.station;
  int pos=RailLogic::position(config.station); doc["locationName"]=pos>=0?RailLogic::stations[pos].name:config.station;
  unsigned offsets[]={4,9,21,36},index=0;
  for(JsonObject train:doc["trainServices"].as<JsonArray>()) {
    time_t planned=now+offsets[index]*60, expected=planned+(index==0?7*60:0);
    struct tm t; char value[6]; localtime_r(&planned,&t); strftime(value,sizeof(value),"%H:%M",&t); train["std"]=value;
    if(index==0) { localtime_r(&expected,&t); strftime(value,sizeof(value),"%H:%M",&t); train["etd"]=value; }
    ++index;
  }
  const auto& rule=activeRule(now);bool arrivals=rule.mode==RailSchedule::ARRIVALS;
  for(JsonObject train:doc["trainServices"].as<JsonArray>()) {
    train["sta"]=train["std"];train["eta"]=train["etd"];
    const char* crs=train["origin"][0]["crs"] | "";int origin=RailLogic::position(crs);
    train["origin"][0]["locationName"]=origin>=0?RailLogic::stations[origin].name:crs;
  }
  const char* error=nullptr;
  if(RailBoard::parse(doc.as<JsonObjectConst>(),config.station,rule.route,config.operators,now,board,error,arrivals)) { hasData=true;strcpy(boardRoute,rule.route);fetchError=""; }
  else fetchError=error;
}
void waitBeforeRetry(uint32_t seconds) {
  seconds=min(seconds,RttPolicy::MAX_WAIT_SECONDS);
  uint32_t candidate=millis()+seconds*1000u;
  if(!retryPending || int32_t(candidate-retryAt)>0)retryAt=candidate;
  retryPending=true;
}
void respectRateLimits(HTTPClient& http) {
  const char* headers[]={"X-RateLimit-Remaining-Minute","X-RateLimit-Remaining-Hour","X-RateLimit-Remaining-Day","X-RateLimit-Remaining-Week"};
  const uint32_t seconds[]={60,3600,86400,604800};
  for(unsigned i=0;i<4;++i)if(http.header(headers[i])=="0") {quotaBlocked=true;waitBeforeRetry(seconds[i]);}
}
bool beginRtt(HTTPClient& http,BearSSL::WiFiClientSecure& client,const String& path,const String& bearer) {
  if(quietHours(time(nullptr))) {fetchError="";return false;}
  http.setTimeout(8000);http.useHTTP10(true);http.setReuse(false);
  if(!http.begin(client,String(RAIL_BASE)+path)) {fetchError="Could not start RTT connection";return false;}
  const char* headers[]={"Retry-After","X-RateLimit-Remaining-Minute","X-RateLimit-Remaining-Hour","X-RateLimit-Remaining-Day","X-RateLimit-Remaining-Week"};
  http.collectHeaders(headers,5);http.addHeader("Authorization","Bearer "+bearer);http.addHeader("Accept","application/json");
  http.addHeader("Version","2026-04-09");return true;
}
bool responseOk(HTTPClient& http,int response,bool allowEmpty=false) {
  respectRateLimits(http);
  if(response==HTTP_CODE_OK || (allowEmpty && response==HTTP_CODE_NO_CONTENT))return true;
  if(response==429) {waitBeforeRetry(RttPolicy::retrySeconds(http.header("Retry-After").c_str()));fetchError="RTT rate limit - waiting before retry";}
  else if(response==401 || response==403) {fetchError="RTT authentication failed - check refresh token";accessToken="";accessValidUntil=0;}
  else fetchError=response<0?"RTT secure connection failed":"RTT returned HTTP "+String(response);
  return false;
}
bool renewAccess(BearSSL::WiFiClientSecure& client) {
  HTTPClient http;
  if(!beginRtt(http,client,"/api/get_access_token",config.refreshToken))return false;
  int response=http.GET();
  if(!responseOk(http,response)) {http.end();return false;}
  if(http.getSize()>8192 || ESP.getFreeHeap()<6500 || ESP.getMaxFreeBlockSize()<4608) {fetchError="RTT token response exceeds available memory";http.end();return false;}
  StaticJsonDocument<128> filter;filter["token"]=true;filter["validUntil"]=true;
  DynamicJsonDocument doc(4096);LimitedStream stream(http.getStream(),8192);
  auto error=deserializeJson(doc,stream,DeserializationOption::Filter(filter));
  const char* token=doc["token"] | "";time_t until=RailLogic::isoTime(doc["validUntil"] | "");
  if(error || !RttPolicy::validToken(token) || strlen(token)>3072 || until<=time(nullptr)+60) {
    fetchError="Invalid RTT access token response";http.end();return false;
  }
  http.end();accessToken=token;accessValidUntil=until;return true;
}
void fetchBoard() {
  if(quietHours(time(nullptr)))return;
  // Settings saves and failed requests obey the same quota spacing as polling.
  if(retryPending && !RttPolicy::due(millis(),retryAt))return;
  if(!RailSchedule::requestDue(millis(),lastFetch,attemptedFetch,activeRule(time(nullptr)).interval,fetchPending))return;
  retryPending=false;quotaBlocked=false;lastFetch=millis();attemptedFetch=true;fetchPending=false;
  if(WiFi.status()!=WL_CONNECTED) {fetchError="Wi-Fi disconnected";return;}
  time_t now=time(nullptr);
  if(now<1700000000) {fetchError="Syncing clock for secure RTT connection";retryAt=millis()+5000;retryPending=true;attemptedFetch=false;fetchPending=true;return;}
  if(config.demo) {makeDemo();return;}
  if(!config.refreshToken[0]) {fetchError="Configure RTT refresh token in the web UI";return;}
  if(ESP.getFreeHeap()<20000) {fetchError="Insufficient memory for a secure RTT request";return;}
  BearSSL::WiFiClientSecure client;
  BearSSL::X509List roots(RTT_ROOT_CA);client.setTrustAnchors(&roots);client.setTimeout(8000);
  if(!mflnChecked) {mflnSupported=client.probeMaxFragmentLength("data.rtt.io",443,1024);mflnChecked=true;}
  client.setBufferSizes(mflnSupported?1024:16384,512);client.setSSLVersion(BR_TLS12,BR_TLS12);
  if(accessValidUntil<=now+60 && !renewAccess(client))return;
  // A successful renewal can still consume the last permitted request.
  if(quotaBlocked) {fetchError="RTT quota reached - waiting before board request";return;}
  RailSchedule::Rule requestRule=activeRule(now);bool arrivals=requestRule.mode==RailSchedule::ARRIVALS;
  String path="/rtt/location?code=gb-nr:"+String(config.station)+"&timeWindow=60&timeTolerance=true";
  if(!strncmp(requestRule.route,"to-",3))path+=String(arrivals?"&filterFrom=gb-nr:":"&filterTo=gb-nr:")+String(requestRule.route+3);
  HTTPClient http;if(!beginRtt(http,client,path,accessToken))return;
  int response=http.GET();railHttpStatus=response;railDiagnostics=RttBoard::Diagnostics{};if(!responseOk(http,response,true)) {http.end();return;}
  std::unique_ptr<RailBoard::Snapshot> next(new(std::nothrow) RailBoard::Snapshot{});
  if(!next) {fetchError="Insufficient memory for RTT board";http.end();return;}
  if(response==HTTP_CODE_NO_CONTENT) {
    next->generated=time(nullptr);next->available=true;next->arrivals=arrivals;RailBoard::copy(next->station,sizeof(next->station),config.station);
    int pos=RailLogic::position(config.station);RailBoard::copy(next->stationName,sizeof(next->stationName),pos>=0?RailLogic::stations[pos].name:config.station);
    if(!quietHours(time(nullptr)) && RailSchedule::sameBoard(requestRule,activeRule(time(nullptr)))) {board=*next;hasData=true;strcpy(boardRoute,requestRule.route);fetchError="";}else fetchPending=true;http.end();return;
  }
  if(http.getSize()>131072 || ESP.getFreeHeap()<6000 || ESP.getMaxFreeBlockSize()<2560) {fetchError="RTT response exceeds available memory";http.end();return;}
  DynamicJsonDocument filter(1536),work(2048);LimitedStream stream(http.getStream(),131072);
  const char* error=nullptr;
  if(RttBoard::decode(stream,filter,work,config.station,requestRule.route,config.operators,time(nullptr),*next,error,&railDiagnostics,arrivals)) {
    if(!quietHours(time(nullptr)) && RailSchedule::sameBoard(requestRule,activeRule(time(nullptr)))) {board=*next;hasData=true;strcpy(boardRoute,requestRule.route);fetchError="";}else fetchPending=true;
  } else fetchError=error;
  http.end();
}
void printText(const String& value,int x,int y,int size=1,uint16_t color=FG) {
  tft.setTextFont(1); tft.setTextSize(size); tft.setTextColor(color,BG); tft.setCursor(x,y); tft.print(value);
}
// Use unscaled built-in fonts and measure pixels before fitting each row.
void uiText(const String& value,int x,int y,int font=2,uint16_t color=FG) {
  tft.setTextSize(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(color,BG);
  tft.drawString(value,x,y,font);
}
int uiWidth(const String& value,int font=2) { tft.setTextSize(1); return tft.textWidth(value,font); }
void uiRightText(const String& value,int right,int y,int font=2,uint16_t color=FG) {
  uiText(value,right-uiWidth(value,font),y,font,color);
}
void uiBoldText(const String& value,int x,int y,int font=4,uint16_t color=FG) {
  // Transparent overdraw adds a pixel of weight without erasing the first pass.
  tft.setTextSize(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(color);
  tft.drawString(value,x,y,font); tft.drawString(value,x+1,y,font);
}
String fitText(String value,int width,int font=2) {
  if(uiWidth(value,font)<=width)return value;
  while(value.length() && uiWidth(value+"...",font)>width)value.remove(value.length()-1);
  value.trim(); return value+"...";
}
// Split at word boundaries where possible; long words still make progress.
unsigned lineLength(const String& value,int width,int font=2) {
  unsigned end=0;
  while(end<value.length() && uiWidth(value.substring(0,end+1),font)<=width)++end;
  if(end==value.length())return end;
  int space=value.lastIndexOf(' ',end);
  return space>0?static_cast<unsigned>(space):max(1u,end);
}
String textPage(const String& value,uint32_t phase,int width,int font=2) {
  if(!value.length())return "";
  unsigned pages=0,offset=0;
  while(offset<value.length()) {
    offset+=lineLength(value.substring(offset),width,font);
    while(offset<value.length() && value[offset]==' ')++offset;
    ++pages;
  }
  unsigned wanted=phase%pages; offset=0;
  for(unsigned page=0;page<=wanted;++page) {
    unsigned end=offset+lineLength(value.substring(offset),width,font);
    if(page==wanted)return value.substring(offset,end);
    offset=end; while(offset<value.length() && value[offset]==' ')++offset;
  }
  return "";
}
void destinationText(const String& value) {
  if(uiWidth(value,4)<=210) { uiText(value,20,78,4); return; }
  if(uiWidth(value)<=210) { uiText(value,20,82); return; }
  unsigned split=lineLength(value,210);
  uiText(value.substring(0,split),20,77);
  String remainder=value.substring(split); remainder.trim();
  uiText(fitText(remainder,210),20,94);
}
void field(unsigned index,const String& signature,int y,int height) {
  if(fields[index]==signature) return;
  fields[index]=signature; tft.fillRect(10,y,220,height,BG);
}
bool changed(unsigned index,const String& signature,int y,int height) {
  if(fields[index]==signature) return false;
  field(index,signature,y,height); return true;
}
String clockLabel(time_t now) { struct tm local; localtime_r(&now,&local); char value[6]; strftime(value,sizeof(value),"%H:%M",&local); return value; }
uint16_t trainColor(const RailBoard::Train& train) { return !strcmp(train.operatorCode,"XR")?PURPLE:!strcmp(train.operatorCode,"GW")?GREEN:FG; }
String trainStatus(const RailBoard::Train& train) {
  if(train.forecast.status==RailLogic::CANCELLED) return "CANCELLED";
  if(!train.forecast.expected) return train.forecast.status==RailLogic::DELAYED?"DELAYED / TIME TBC":"TIME UNCONFIRMED";
  if(train.forecast.delay>0) return "+"+String(train.forecast.delay)+" min";
  return "On time";
}
bool cancelledRow(const RailBoard::Train* train) {return train && train->forecast.status==RailLogic::CANCELLED;}
void trainRowText(const RailBoard::Train* train,String& left,String& right) {
  if(!train)return;
  if(cancelledRow(train) && train->reason[0] && ((millis()/6000)%2)) {left=textPage(train->reason,millis()/12000,220);return;}
  left=String(train->scheduled)+" "+train->destination;
  right=cancelledRow(train)?"CANCELLED":train->forecast.expected?String(train->expected)+(train->forecast.delay>0?" +"+String(train->forecast.delay):""):train->forecast.status==RailLogic::DELAYED?"Delayed":"Time TBC";
  if(uiWidth(right)>100)right=cancelledRow(train)?"CANCELLED":train->forecast.status==RailLogic::DELAYED?"Delayed":"Time TBC";
  left=fitText(left,220-uiWidth(right)-8);
}
void render() {
  if(apMode) return;
  if(!screenInitialized) { tft.fillScreen(BG); for(auto& value:fields)value=""; screenInitialized=true; }
  time_t now=time(nullptr); bool quiet=quietHours(now); String error=activeError();
  String name=hasData?board.stationName:RailLogic::position(config.station)>=0?RailLogic::stations[RailLogic::position(config.station)].name:config.station;
  String clock=now>=1700000000?clockLabel(now):"--:--";
  String heading=name+":"+clock;
  if(changed(0,heading,8,27)) {
    uiBoldText(fitText(name,220-uiWidth(clock,4)-12,4),10,8);
    uiBoldText(clock,229-uiWidth(clock,4),8);
  }
  bool arrivals=activeRule(now).mode==RailSchedule::ARRIVALS;
  String subtitle=quiet?"UPDATES PAUSED":error.length()?"UPDATE PROBLEM":arrivals?"NEXT ARRIVAL":"NEXT DEPARTURE";
  String mode=!quiet && config.demo?"DEMO":"";
  if(changed(1,subtitle+":"+mode,37,18)) {
    uiText(subtitle,10,37,2,error.length()?AMBER:MUTED);
    uiRightText(mode,230,37,2,AMBER);
  }
  bool usable=!quiet && boardMatches(now) && !RailLogic::stale(now,board.generated,pollMs()/1000+30) && board.available;
  RailBoard::DisplayRows rows;if(usable)rows=RailBoard::displayRows(board,now);
  const auto* hero=rows.primary;
  String heroIdentity=hero?String(hero->destination)+":"+hero->operatorCode+":"+hero->operatorName+":"+String(hero->forecast.expected)+":"+(arrivals?"arrivals":"departures"):"none:"+error+":"+(hasData?String(board.count):"0")+":"+(usable?"ready":"waiting")+":"+(board.available?"open":"closed")+":"+(quiet?"night":"day");
  if(changed(2,heroIdentity,56,55)) {
    tft.fillRect(10,56,3,105,BG);
    if(hero) {
      uiText(fitText(String(hero->operatorName)+(arrivals?" / from":""),210),20,57,2,trainColor(*hero));
      destinationText(hero->destination);
    } else {
      String message=quiet?"Outside update hours":hasData && !board.available?"Station services unavailable":usable?(board.count?"Waiting for next refresh":"No upcoming train in feed"):arrivals?"Waiting for arrivals":"Waiting for departures";
      unsigned split=lineLength(message,220);
      uiText(message.substring(0,split),10,56,2,MUTED);
      String remainder=message.substring(split); remainder.trim();
      uiText(remainder,10,74,2,MUTED);
      uiText(quiet?"Resumes at "+minutesLabel(config.schedule.start):usable?"Next refresh is automatic":!hasData?"Open the display web UI":"Check connection / web UI",10,94,2,AMBER);
    }
  }
  String count=hero && hero->forecast.expected?String(RailLogic::countdown(hero->forecast.expected,now)):"--";
  if(changed(3,count+":"+(quiet?"night":"day"),112,49) && !quiet) {
    uiText(count,20,112,6);
    uiText(count=="--"?"time TBC":count=="0"?"due":"min",20+uiWidth(count,6)+8,139,2,MUTED);
  }
  if(hero)tft.fillRect(10,56,3,105,trainColor(*hero));
  String meta=hero?String(hero->expected)+":"+hero->scheduled+":"+hero->platform+":"+trainStatus(*hero):"none";
  if(changed(4,meta,166,18) && hero) {
    String left=hero->expected[0]?hero->expected:hero->scheduled;
    left+=hero->platform[0]?"  P"+String(hero->platform):"  P--";
    String status=trainStatus(*hero);
    if(uiWidth(status)>110)status=hero->forecast.status==RailLogic::DELAYED?"Delayed":"Time TBC";
    uiText(fitText(left,220-uiWidth(status)-8),10,166);
    uiRightText(status,230,166,2,hero->forecast.status==RailLogic::ON_TIME?GREEN:AMBER);
  }
  const auto* following=rows.following;
  String row,rowRight;trainRowText(following,row,rowRight);
  uint16_t rowColor=cancelledRow(following)?RED:following?trainColor(*following):MUTED;
  if(changed(5,row+":"+rowRight+":"+String(rowColor),190,22)) {
    tft.drawFastHLine(10,190,220,TRACK);
    uiText(row,10,194,2,rowColor);
    uiRightText(rowRight,230,194,2,cancelledRow(following)?RED:following && following->forecast.status==RailLogic::DELAYED?AMBER:FG);
  }
  const auto* third=rows.third;
  String lower,lowerRight;
  uint16_t lowerColor=MUTED;
  if(cancelledRow(third)) {trainRowText(third,lower,lowerRight);lowerColor=RED;}
  else if(error.length()) {lower=textPage(error,millis()/6000,220);lowerColor=AMBER;}
  else if(hero && hero->reason[0]) {lower=textPage(hero->reason,millis()/6000,220);lowerColor=AMBER;}
  else if(!quiet && board.message[0])lower=textPage(board.message,millis()/6000,220);
  else if(third) {trainRowText(third,lower,lowerRight);lowerColor=trainColor(*third);}
  if(changed(6,lower+":"+lowerRight+":"+String(lowerColor),217,18)) {
    uiText(lower,10,217,2,lowerColor);
    uiRightText(lowerRight,230,217,2,lowerColor);
  }
  lastRender=millis();
}
bool loadWifi(String& ssid,String& pass) {
  EEPROM.begin(EEPROM_SIZE); char bytes[65]={};
  for(unsigned i=0;i<32;++i)bytes[i]=EEPROM.read(i); ssid=bytes; memset(bytes,0,sizeof(bytes));
  for(unsigned i=0;i<64;++i)bytes[i]=EEPROM.read(i+32); pass=bytes; EEPROM.end();
  return ssid.length() && static_cast<unsigned char>(ssid[0])!=255;
}
void setup() {
  loadSettings(); tft.init(); tft.setRotation(0); tft.fillScreen(BG);
  pinMode(TFT_BL,OUTPUT); analogWriteRange(1023); analogWriteFreq(1000); applyBrightness(brightness);
  WiFi.persistent(false); String ssid,pass;
  if(loadWifi(ssid,pass)) {
    WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(),pass.c_str()); printText("Connecting Wi-Fi...",10,104,2);
    uint32_t start=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-start<15000)delay(50);
  }
  apMode=WiFi.status()!=WL_CONNECTED;
  if(apMode) {
    WiFi.mode(WIFI_AP); WiFi.softAP(AP_SSID,AP_PASS); tft.fillScreen(BG);
    printText("Wi-Fi setup",10,22,2); printText("Join MiniScreen-Setup",10,65); printText("Password: 12345678",10,95); printText("Open 192.168.4.1",10,125);
  } else {
    WiFi.setAutoReconnect(true); configTime("GMT0BST,M3.5.0/1,M10.5.0","0.pool.ntp.org","1.pool.ntp.org");
    render();
  }
  updater.setup(&server,"/update",OTA_USERNAME,OTA_PASSWORD);
  server.on("/",HTTP_GET,[](){server.send_P(200,"text/html",apMode?WIFI_HTML:TRAIN_HTML);});
  server.on("/wifi",HTTP_GET,[](){server.send_P(200,"text/html",WIFI_HTML);});
  server.on("/settings",HTTP_GET,[](){server.send(200,"application/json",settingsJson());});
  server.on("/settings",HTTP_POST,handleSettings);
  server.on("/stations",HTTP_GET,sendStations);
  server.on("/diagnostics",HTTP_GET,[](){server.send(200,"application/json",diagnosticsJson());});
  server.on("/state",HTTP_GET,[](){server.send(200,"application/json",stateJson());});
  server.on("/brightness",HTTP_POST,[](){
    String value=server.arg("value"); bool ok=value.length()>0 && value.length()<=3;
    for(unsigned i=0;i<value.length();++i)if(!isdigit(value[i]))ok=false;
    unsigned number=value.toInt();
    if(!ok || number>100) {server.send(400,"application/json","{\"error\":\"Brightness must be 0 to 100\"}");return;}
    applyBrightness(number); brightnessPending=true; brightnessChangedAt=millis(); server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/connect",HTTP_POST,[](){
    String ssid=server.arg("ssid"),pass=server.arg("pass");
    if(!ssid.length() || ssid.length()>32 || pass.length()>63) {server.send(400,"text/plain","Invalid Wi-Fi credentials");return;}
    EEPROM.begin(EEPROM_SIZE);
    for(unsigned i=0;i<32;++i)EEPROM.write(i,i<ssid.length()?ssid[i]:0);
    for(unsigned i=0;i<64;++i)EEPROM.write(i+32,i<pass.length()?pass[i]:0);
    bool saved=EEPROM.commit();EEPROM.end();
    if(!saved){server.send(500,"text/plain","Could not save Wi-Fi");return;}
    server.send(200,"text/plain","Saved. Rebooting; reconnect to your normal Wi-Fi.");delay(500);ESP.restart();
  });
  server.onNotFound([](){server.send(404,"text/plain","not found");});server.begin();
}
void loop() {
  server.handleClient(); persistBrightness();
  reconcileSchedule();bool quiet=quietHours(time(nullptr));
  if(!apMode && !quiet && (fetchPending || millis()-lastFetch>=pollMs()))fetchBoard();
  if(!apMode && millis()-lastRender>=1000)render();
  yield();
}
