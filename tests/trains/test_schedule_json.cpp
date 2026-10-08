#include <cassert>
#include <fstream>
#include "../../firmware/esp_train_departures/rail_config_json.h"
int main() {
  DynamicJsonDocument doc(4096);const char* error=nullptr;RailSchedule::Schedule s;
  assert(!deserializeJson(doc,R"JSON({"start":"06:00","end":"22:00","allDay":false,"default":{"mode":"departures","intervalMinutes":5,"direction":"east"},"rules":[{"start":"06:00","end":"09:00","mode":"departures","intervalMinutes":1,"direction":"east"},{"start":"17:00","end":"19:00","mode":"arrivals","intervalMinutes":2,"direction":"to-RDG"}]})JSON"));
  auto parse=[&](){return RailConfigJson::readSchedule(doc.as<JsonObjectConst>(),"PAD",s,error);};
  assert(parse() && s.count==2 && s.rules[1].mode==RailSchedule::ARRIVALS && RailSchedule::dailyRequests(s)==372);
  doc["rules"][1]["direction"]="to-PAD";assert(!parse());doc["rules"][1]["direction"]="east";
  doc["rules"][1]["start"]="08:59";assert(!parse());doc["rules"][1]["start"]="09:00";assert(parse());
  doc["rules"][0]["intervalMinutes"]=1.5;assert(!parse());doc["rules"][0]["intervalMinutes"]=true;assert(!parse());
  doc["rules"][0]["intervalMinutes"]="1";assert(!parse());doc["rules"][0]["intervalMinutes"]=1;
  doc["rules"][0]["mode"]="both";assert(!parse());doc["rules"][0]["mode"]="arrivals";
  doc["rules"][0]["start"]="25:00";assert(!parse());doc["rules"][0]["start"]="06:00";
  doc["allDay"]="false";assert(!parse());doc["allDay"]=false;
  doc["default"]["direction"]="a-very-long-route";assert(!parse());doc["default"]["direction"]="both";
  doc["rules"].as<JsonArray>().clear();assert(parse()&&s.count==0);
  for(int i=0;i<9;++i){JsonObject r=doc["rules"].createNestedObject();r["start"]="06:00";r["end"]="07:00";r["mode"]="departures";r["intervalMinutes"]=5;r["direction"]="both";}
  assert(!parse());doc["rules"].as<JsonArray>().clear();doc["rules"].add(4);assert(!parse());
  std::ifstream example("examples/train-schedule.json");assert(example.good());
  assert(!deserializeJson(doc,example));
  assert(!strcmp(doc["station"] | "","PAD"));
  assert(RailConfigJson::readSchedule(doc["schedule"].as<JsonObjectConst>(),"PAD",s,error));
  assert(s.count==2 && s.rules[0].mode==RailSchedule::DEPARTURES && s.rules[1].mode==RailSchedule::ARRIVALS && RailSchedule::dailyRequests(s)==384);
  puts("Schedule JSON schema, invalid intervals/routes, overlap and bounded rule validation passed.");
}
