#pragma once
#include "rail_board.h"

namespace RttBoard {
struct Diagnostics {
  unsigned seen=0,nonPassenger=0,operatorFiltered=0,directionFiltered=0,nonCalling=0,invalidTime=0,departed=0,expired=0;
  char sample[1024]={};
};
inline void locationFilter(JsonObject node) {
  node["namespace"]=true; node["description"]=true; node["shortCodes"][0]=true;
}
inline void serviceFilter(JsonDocument& filter,bool arrivals=false) {
  filter.clear();
  for(const char* key:{"scheduledCallType","realtimeCallType","displayAs"})filter["temporalData"][key]=true;
  for(const char* key:{"scheduleAdvertised","realtimeForecast","realtimeEstimate","realtimeActual","realtimeNoReport","isCancelled"})filter["temporalData"][arrivals?"arrival":"departure"][key]=true;
  filter["scheduleMetadata"]["operator"]["code"]=true; filter["scheduleMetadata"]["operator"]["name"]=true;
  filter["scheduleMetadata"]["modeType"]=true; filter["scheduleMetadata"]["inPassengerService"]=true;
  filter["locationMetadata"]["platform"]["actual"]=true; filter["locationMetadata"]["platform"]["planned"]=true;
  for(const char* key:{"origin","destination"})locationFilter(filter[key][0]["location"].to<JsonObject>());
  for(const char* key:{"type","shortText","longText"})filter["reasons"][0][key]=true;
}
inline const char* shortCode(JsonObjectConst location) {
  for(const char* code:location["shortCodes"].as<JsonArrayConst>())if(RailLogic::crs(code))return code;
  return "";
}
// Basic RTT line-ups can omit codes from origin/destination locations.
// Resolve only exact catalog names/aliases; never guess an unknown route.
inline const char* locationCode(JsonObjectConst location) {
  const char* code=shortCode(location);if(*code)return code;
  const char* name=location["description"] | "";
  for(unsigned i=0;i<RailLogic::STATION_COUNT;++i)if(!strcmp(name,RailLogic::stations[i].name))return RailLogic::stations[i].crs;
  if(!strcmp(name,"Paddington"))return "PAD";
  if(!strcmp(name,"Liverpool Street"))return "LST";
  if(!strcmp(name,"Heathrow Airport Terminal 4"))return "HAF";
  if(!strcmp(name,"Heathrow Airport Terminal 5"))return "HWV";
  if(!strcmp(name,"Heathrow Airport Terminals 2 & 3"))return "HXX";
  return "";
}
inline time_t rttTime(const char* value) {
  time_t zoned=RailLogic::isoTime(value);if(zoned || !value)return zoned;
  // Observed RTT train timestamps omit the offset and use location-local time.
  size_t length=strlen(value);if(length<19 || length>40)return 0;
  const char* tail=value+19;
  if(*tail=='.') {++tail;const char* fraction=tail;while(isdigit(*tail))++tail;if(tail==fraction)return 0;}
  if(*tail)return 0;
  char utcShape[21];memcpy(utcShape,value,19);utcShape[19]='Z';utcShape[20]=0;
  if(!RailLogic::isoTime(utcShape))return 0;
  int year,month,day,hour,minute,second;
  if(sscanf(value,"%d-%d-%dT%d:%d:%d",&year,&month,&day,&hour,&minute,&second)!=6)return 0;
  struct tm local={};local.tm_year=year-1900;local.tm_mon=month-1;local.tm_mday=day;
  local.tm_hour=hour;local.tm_min=minute;local.tm_sec=second;local.tm_isdst=-1;
  time_t result=mktime(&local);
  // Reject nonexistent local times rather than silently moving them forward.
  if(local.tm_year!=year-1900 || local.tm_mon!=month-1 || local.tm_mday!=day ||
     local.tm_hour!=hour || local.tm_min!=minute || local.tm_sec!=second)return 0;
  return result>0?result:0;
}
inline bool validLocation(JsonObjectConst location) { return !strcmp(location["namespace"] | "","gb-nr"); }
inline RailLogic::Direction directionOf(const char* station,JsonObjectConst service,bool arrivals=false) {
  if(arrivals) {
    for(JsonObjectConst pair:service["origin"].as<JsonArrayConst>()) {
      auto dir=RailLogic::towards(station,locationCode(pair["location"]));if(dir!=RailLogic::UNKNOWN)return dir;
    }
    for(JsonObjectConst pair:service["destination"].as<JsonArrayConst>()) {
      auto dir=RailLogic::fromOrigin(station,locationCode(pair["location"]));if(dir!=RailLogic::UNKNOWN)return dir;
    }
    return RailLogic::UNKNOWN;
  }
  for(JsonObjectConst pair:service["destination"].as<JsonArrayConst>()) {
    auto dir=RailLogic::towards(station,locationCode(pair["location"]));
    if(dir!=RailLogic::UNKNOWN)return dir;
  }
  for(JsonObjectConst pair:service["origin"].as<JsonArrayConst>()) {
    auto dir=RailLogic::fromOrigin(station,locationCode(pair["location"]));
    if(dir!=RailLogic::UNKNOWN)return dir;
  }
  return RailLogic::UNKNOWN;
}
inline bool matches(const char* station,const char* choice,JsonObjectConst service,bool arrivals=false) {
  // A to-XXX request is already filtered by RTT, including intermediate calls.
  if(!strcmp(choice,"both") || !strncmp(choice,"to-",3))return true;
  auto dir=directionOf(station,service,arrivals);
  return (!strcmp(choice,"east") && dir==RailLogic::EAST) || (!strcmp(choice,"west") && dir==RailLogic::WEST) || (!strcmp(choice,"other") && dir==RailLogic::UNKNOWN);
}
inline void timeLabel(char* output,time_t when) {
  if(!when) {output[0]=0;return;}
  struct tm local;localtime_r(&when,&local);strftime(output,6,"%H:%M",&local);
}
inline void addService(JsonObjectConst service,const char* station,const char* direction,const char* operators,time_t now,
                       RailBoard::Snapshot& result,unsigned& nr,unsigned& nu,unsigned& nc,Diagnostics* diagnostics=nullptr,bool arrivals=false) {
  if(diagnostics)++diagnostics->seen;
  JsonObjectConst schedule=service["scheduleMetadata"];
  // Missing optional flags must not hide a service explicitly advertised for
  // boarding. Explicit non-passenger and non-train values still exclude it.
  if((schedule["inPassengerService"].is<bool>() && !schedule["inPassengerService"].as<bool>()) ||
     (!schedule["modeType"].isNull() && strcmp(schedule["modeType"] | "","TRAIN"))) {
    if(diagnostics)++diagnostics->nonPassenger;return;
  }
  const char* code=schedule["operator"]["code"] | "";
  if(!RailBoard::operatorAllowed(operators,code)) {if(diagnostics)++diagnostics->operatorFiltered;return;}
  if(!matches(station,direction,service,arrivals)) {if(diagnostics)++diagnostics->directionFiltered;return;}
  JsonObjectConst temporal=service["temporalData"], activity=temporal[arrivals?"arrival":"departure"];
  const char* display=temporal["displayAs"] | "";
  bool cancelled=(activity["isCancelled"] | false) || !strcmp(display,"CANCELLED") || !strcmp(display,"DIVERTED");
  const char* call=temporal["realtimeCallType"] | temporal["scheduledCallType"] | "";
  const char* plannedCall=temporal["scheduledCallType"] | "";
  const char* oneWay=arrivals?"ADVERTISED_SET_DOWN":"ADVERTISED_PICK_UP";
  if(cancelled && strcmp(plannedCall,"ADVERTISED_OPEN") && strcmp(plannedCall,oneWay)) {if(diagnostics)++diagnostics->nonCalling;return;}
  if(!cancelled && (strcmp(call,"ADVERTISED_OPEN") && strcmp(call,oneWay))) {if(diagnostics)++diagnostics->nonCalling;return;}
  if(!cancelled && strcmp(display,"CALL") && strcmp(display,arrivals?"TERMINATES":"STARTS")) {if(diagnostics)++diagnostics->nonCalling;return;}
  time_t planned=rttTime(activity["scheduleAdvertised"] | "");
  if(!planned || planned<now-24*3600 || planned>now+2*3600) {if(diagnostics)++diagnostics->invalidTime;return;}
  if(*(activity["realtimeActual"] | "")) {if(diagnostics)++diagnostics->departed;return;}
  RailBoard::Train train={}; train.forecast={RailLogic::UNCONFIRMED,planned,0,-1};
  if(cancelled)train.forecast.status=RailLogic::CANCELLED;
  else {
    // No report means a forecast may no longer be credible. Use an explicit
    // entitled estimate if supplied, otherwise leave the countdown unknown.
    time_t expected=rttTime(activity[(activity["realtimeNoReport"] | false)?"realtimeEstimate":"realtimeForecast"] | "");
    if(!expected)expected=rttTime(activity["realtimeEstimate"] | "");
    if(expected && expected<now) {if(diagnostics)++diagnostics->expired;return;}
    if(expected && expected<now+24*3600) {
      train.forecast.expected=expected;
      train.forecast.delay=int((expected-planned)/60);
      train.forecast.status=train.forecast.delay>0?RailLogic::DELAYED:RailLogic::ON_TIME;
      if(train.forecast.delay<0)train.forecast.delay=0;
    }
  }
  if(!train.forecast.expected && planned<now-(cancelled?0:120)) {if(diagnostics)++diagnostics->expired;return;}
  RailBoard::copy(train.destination,sizeof(train.destination),service[arrivals?"origin":"destination"][0]["location"]["description"] | (arrivals?"Origin unavailable":"Destination unavailable"));
  if(!strcmp(train.destination,"London Paddington"))RailBoard::copy(train.destination,sizeof(train.destination),"Paddington");
  RailBoard::copy(train.operatorName,sizeof(train.operatorName),!strcmp(code,"GW")?"GWR":!strcmp(code,"XR")?"Elizabeth line":schedule["operator"]["name"] | "Rail service");
  RailBoard::copy(train.operatorCode,sizeof(train.operatorCode),code);
  timeLabel(train.scheduled,planned);timeLabel(train.expected,train.forecast.expected);
  RailBoard::copy(train.platform,sizeof(train.platform),service["locationMetadata"]["platform"]["actual"] | service["locationMetadata"]["platform"]["planned"] | "");
  for(JsonObjectConst reason:service["reasons"].as<JsonArrayConst>()) {
    bool cancellation=!strcmp(reason["type"] | "","CANCEL");
    if(cancellation!=cancelled)continue;
    const char* text=reason["longText"] | "";
    if(!*text)text=reason["shortText"] | "";
    RailBoard::copy(train.reason,sizeof(train.reason),text);
    if(!RailLogic::usefulReason(train.reason)) {train.reason[0]=0;continue;}
    if(!cancelled && !train.forecast.expected)train.forecast.status=RailLogic::DELAYED;
    if(train.reason[0])break;
  }
  if(cancelled)RailBoard::insert(result.trains+RailBoard::CANCELLED_OFFSET,nc,RailBoard::CANCELLED_LIMIT,train,false);
  else if(train.forecast.expected)RailBoard::insert(result.trains,nr,RailBoard::PREDICTED_LIMIT,train,true);
  else RailBoard::insert(result.trains+RailBoard::UNCONFIRMED_OFFSET,nu,RailBoard::UNCONFIRMED_LIMIT,train,false);
}
template<class Input> int nextChar(Input& input) {
  char ch;
  while(input.readBytes(&ch,1)==1)if(ch!=' ' && ch!='\r' && ch!='\n' && ch!='\t')return static_cast<unsigned char>(ch);
  return -1;
}
// Decode one service at a time. Memory does not grow with station traffic;
// malformed/truncated envelopes never replace the last complete snapshot.
template<class Input> bool decode(Input& input,JsonDocument& filter,JsonDocument& work,const char* station,const char* direction,
                                 const char* operators,time_t now,RailBoard::Snapshot& result,const char*& error,Diagnostics* diagnostics=nullptr,bool arrivals=false) {
  if(diagnostics)*diagnostics=Diagnostics{};
  result={};result.generated=now;result.available=true;result.arrivals=arrivals;
  RailBoard::copy(result.station,sizeof(result.station),station);
  unsigned nr=0,nu=0,nc=0;bool foundQuery=false,foundServices=false,foundStatus=false,live=true;
  if(nextChar(input)!='{') {error="Invalid RTT response";return false;}
  while(true) {
    StaticJsonDocument<96> key;
    if(deserializeJson(key,input) || !key.is<const char*>()) {error="Invalid RTT field";return false;}
    if(nextChar(input)!=':') {error="Invalid RTT field separator";return false;}
    const char* name=key.as<const char*>();filter.clear();
    if(!strcmp(name,"services")) {
      if(foundServices || nextChar(input)!='[') {error="Invalid RTT service list";return false;}
      foundServices=true;serviceFilter(filter,arrivals);
      if(filter.overflowed()) {error="RTT filter exceeds memory";return false;}
      int delimiter=nextChar(input);
      while(delimiter!=']') {
        if(delimiter!='{') {error="Invalid RTT service";return false;}
        // The opening brace has been consumed: feed it back to ArduinoJson.
        struct PrefixedInput {
          Input& source;bool prefix;
          explicit PrefixedInput(Input& value):source(value),prefix(true) {}
          int read(){if(prefix){prefix=false;return '{';}char ch;return source.readBytes(&ch,1)==1?static_cast<unsigned char>(ch):-1;}
          size_t readBytes(char* out,size_t length){size_t count=0;if(prefix && length){*out++='{';prefix=false;--length;++count;}return count+source.readBytes(out,length);}
        } item(input);
        work.clear();
        if(deserializeJson(work,item,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(12))) {error="Invalid or oversized RTT service";return false;}
        if(diagnostics && !diagnostics->sample[0])serializeJson(work,diagnostics->sample,sizeof(diagnostics->sample));
        addService(work.template as<JsonObjectConst>(),station,direction,operators,now,result,nr,nu,nc,diagnostics,arrivals);
        delimiter=nextChar(input);
        if(delimiter==',') {delimiter=nextChar(input);if(delimiter==']'){error="Invalid RTT service separator";return false;}}
        else if(delimiter!=']') {error="Truncated RTT service list";return false;}
      }
    } else {
      if(!strcmp(name,"query"))locationFilter(filter["location"].to<JsonObject>());
      else if(!strcmp(name,"systemStatus")) {filter["realtimeNetworkRail"]=true;filter["rttCore"]=true;}
      else if(!strcmp(name,"reasons")) {filter[0]["shortText"]=true;filter[0]["longText"]=true;}
      work.clear();
      if(deserializeJson(work,input,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(12))) {error="Invalid RTT response data";return false;}
      if(!strcmp(name,"query")) {
        JsonObjectConst location=work["location"];
        if(foundQuery || !validLocation(location) || strcmp(shortCode(location),station)) {error="RTT returned a different station";return false;}
        foundQuery=true;RailBoard::copy(result.stationName,sizeof(result.stationName),location["description"] | station);
      } else if(!strcmp(name,"systemStatus")) {
        if(foundStatus) {error="Duplicate RTT system status";return false;}
        foundStatus=true;
        live=!strcmp(work["realtimeNetworkRail"] | "","OK") && !strcmp(work["rttCore"] | "","OK");
        if(!live)RailBoard::copy(result.message,sizeof(result.message),"RTT live data limited or unavailable");
      } else if(!strcmp(name,"reasons") && !result.message[0]) {
        const char* reason=work[0]["longText"] | work[0]["shortText"] | "";
        RailBoard::copy(result.message,sizeof(result.message),reason);
      }
    }
    int delimiter=nextChar(input);
    if(delimiter=='}')break;
    if(delimiter!=',') {error="Truncated RTT response";return false;}
  }
  if(!foundQuery || !foundServices || !foundStatus) {error="Incomplete RTT response";return false;}
  result.runningCount=nr;result.count=nr;
  for(unsigned i=0;i<nu;++i)result.trains[result.count++]=result.trains[RailBoard::UNCONFIRMED_OFFSET+i];
  for(unsigned i=0;i<nc;++i)result.trains[result.count++]=result.trains[RailBoard::CANCELLED_OFFSET+i];
  if(!live)for(unsigned i=0;i<result.count;++i)if(result.trains[i].forecast.status!=RailLogic::CANCELLED) {
    result.trains[i].forecast.status=RailLogic::UNCONFIRMED;result.trains[i].forecast.expected=0;result.trains[i].expected[0]=0;result.trains[i].forecast.delay=-1;
  }
  return true;
}
}
