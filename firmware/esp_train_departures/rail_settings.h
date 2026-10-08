#pragma once
#include "rail_schedule.h"
#include "rtt_policy.h"
namespace RailConfig {
constexpr uint32_t MAGIC=0x54524E33;
struct Settings { char refreshToken[2049],station[4],operators[10]; uint8_t demo; RailSchedule::Schedule schedule; };
struct Stored {uint32_t magic;Settings settings;uint32_t checksum;};
struct LegacyV2 {char refreshToken[2049],station[4],direction[8],operators[10];uint8_t demo,intervalMinutes;uint16_t updateStart,updateEnd;};
struct LegacyStoredV2 {uint32_t magic;LegacyV2 settings;uint32_t checksum;};
inline uint32_t bytesChecksum(const void* value,size_t size) {
  uint32_t hash=2166136261u;
  for(size_t i=0;i<size;++i)hash=(hash^reinterpret_cast<const uint8_t*>(value)[i])*16777619u;
  return hash;
}
inline bool validOperators(const char* v) {return !strcmp(v,"all") || !strcmp(v,"gwr") || !strcmp(v,"elizabeth") || !strcmp(v,"any");}
inline Settings defaults() {
  Settings s={};strcpy(s.station,"PAD");strcpy(s.operators,"all");s.schedule.start=360;s.schedule.end=1380;
  s.schedule.fallback.interval=3;strcpy(s.schedule.fallback.route,"both");return s;
}
inline bool valid(const Settings& s) {
  const char* error=nullptr;
  return s.refreshToken[2048]==0 && s.station[3]==0 && s.operators[9]==0 && s.demo<=1 &&
    (!s.refreshToken[0] || RttPolicy::validToken(s.refreshToken)) && RailLogic::crs(s.station) && validOperators(s.operators) && RailSchedule::valid(s.schedule,s.station,error);
}
inline bool migrate(const LegacyV2& old,Settings& s) {
  if(old.refreshToken[2048] || old.station[3] || old.direction[7] || old.operators[9] || old.demo>1 ||
     (old.refreshToken[0] && !RttPolicy::validToken(old.refreshToken)) ||
     !RailLogic::crs(old.station) || !RailLogic::directionValid(old.station,old.direction) || !validOperators(old.operators) ||
     !RttPolicy::validInterval(old.intervalMinutes) || old.updateStart>=1440 || old.updateEnd>=1440)return false;
  s=defaults();memcpy(s.refreshToken,old.refreshToken,sizeof(s.refreshToken));strcpy(s.station,old.station);strcpy(s.operators,old.operators);s.demo=old.demo;
  s.schedule.start=old.updateStart;s.schedule.end=old.updateEnd;s.schedule.allDay=old.updateStart==old.updateEnd;
  s.schedule.fallback.interval=old.intervalMinutes;strcpy(s.schedule.fallback.route,old.direction);return valid(s);
}
}
