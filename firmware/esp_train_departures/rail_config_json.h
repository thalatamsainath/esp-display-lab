#pragma once
#include <ArduinoJson.h>
#include "rail_settings.h"
namespace RailConfigJson {
inline bool minute(JsonVariantConst v,uint16_t& out) {
  int hour,part;if(!v.is<const char*>() || !RailLogic::clock(v.as<const char*>(),hour,part))return false;
  out=hour*60+part;return true;
}
inline bool readRule(JsonObjectConst node,RailSchedule::Rule& r,bool window) {
  if(node.isNull() || !node["mode"].is<const char*>() || !node["direction"].is<const char*>() || !node["intervalMinutes"].is<unsigned>())return false;
  const char* mode=node["mode"],*route=node["direction"];
  if(strcmp(mode,"arrivals") && strcmp(mode,"departures"))return false;
  unsigned interval=node["intervalMinutes"];
  if(!RailSchedule::validInterval(interval) || strlen(route)>=sizeof(r.route))return false;
  r.mode=!strcmp(mode,"arrivals")?RailSchedule::ARRIVALS:RailSchedule::DEPARTURES;r.interval=interval;strcpy(r.route,route);
  return !window || (minute(node["start"],r.start) && minute(node["end"],r.end));
}
inline bool readSchedule(JsonObjectConst node,const char* station,RailSchedule::Schedule& s,const char*& error) {
  s={};error="Invalid schedule. Reload the web UI and check all fields";
  if(node.isNull() || !minute(node["start"],s.start) || !minute(node["end"],s.end) ||
     !node["allDay"].is<bool>() || !node["rules"].is<JsonArrayConst>() || !readRule(node["default"],s.fallback,false))return false;
  s.allDay=node["allDay"].as<bool>();
  for(JsonObjectConst r:node["rules"].as<JsonArrayConst>()) {
    if(s.count>=RailSchedule::MAX_RULES || !readRule(r,s.rules[s.count],true))return false;
    ++s.count;
  }
  return RailSchedule::valid(s,station,error);
}
}
