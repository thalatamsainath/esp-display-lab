#include <cassert>
#include <cstdlib>
#include <cstring>
#include "../../firmware/esp_train_departures/rail_settings.h"
using namespace RailSchedule;
int main() {
  setenv("TZ","GMT0BST,M3.5.0/1,M10.5.0",1);tzset();
  auto cfg=RailConfig::defaults();auto& s=cfg.schedule;const char* error=nullptr;
  assert(RailConfig::valid(cfg) && !strcmp(cfg.station,"PAD"));s.end=1320;s.fallback.interval=5;strcpy(s.fallback.route,"east");
  s.count=2;s.rules[0]={360,540,1,DEPARTURES,"east"};s.rules[1]={1020,1140,2,ARRIVALS,"east"};
  assert(valid(s,"PAD",error) && dailyRequests(s)==372);
  assert(at(s,359u).paused && at(s,360u).index==0 && at(s,539u).index==0);
  assert(at(s,540u).index==-1 && at(s,1019u).index==-1 && at(s,1020u).index==1);
  assert(rule(s,at(s,1139u).index).mode==ARRIVALS && at(s,1140u).index==-1 && at(s,1320u).paused);
  assert(at(s,RailLogic::isoTime("2026-10-08T17:30:00+01:00")).index==1);
  assert(at(s,RailLogic::isoTime("2026-12-01T17:30:00Z")).index==1);
  assert(!at(s,time_t(0)).paused); // NTP must precede API traffic, not fake quiet hours.
  s.allDay=1;s.fallback.interval=1;assert(dailyRequests(s)==1380);
  s.rules[1].start=539;assert(!valid(s,"PAD",error));s.rules[1].start=540;assert(valid(s,"PAD",error));
  s.rules[1].start=1020;s.rules[1].end=1020;assert(!valid(s,"PAD",error));s.rules[1].end=1140;
  s.count=1;s.rules[0]={1380,360,7,ARRIVALS,"west"};s.fallback.interval=60;
  assert(valid(s,"PAD",error));assert(at(s,1439u).index==0 && at(s,0u).index==0 && at(s,360u).index==-1);
  assert(dailyRequests(s)==60+17); // Join the overnight interval across midnight.
  s.allDay=0;s.start=1320;s.end=420;assert(valid(s,"PAD",error));assert(dailyRequests(s)==60+2);
  s.rules[1]={300,390,2,DEPARTURES,"east"};s.count=2;assert(!valid(s,"PAD",error));
  s.count=0;s.start=s.end=0;assert(!valid(s,"PAD",error));s.allDay=1;assert(valid(s,"PAD",error));
  s.fallback.interval=1;assert(dailyRequests(s)==1440);s.fallback.interval=60;assert(dailyRequests(s)==24);
  assert(validInterval(1)&&validInterval(60)&&!validInterval(0)&&!validInterval(61));
  s.fallback.mode=2;assert(!valid(s,"PAD",error));s.fallback.mode=DEPARTURES;
  s.count=9;assert(!valid(s,"PAD",error));s.count=0;
  assert(requestDue(0,0,false,5,false));assert(!requestDue(59999,0,true,5,true));
  assert(requestDue(60000,0,true,5,true)&&!requestDue(60000,0,true,5,false));
  assert(requestDue(300000,0,true,5,false));assert(requestDue(60000,0,true,1,false));
  assert(!requestDue(10,0xfffffff0,true,1,true));assert(requestDue(60000,0xfffffff0,true,1,true));
  RailConfig::LegacyV2 old={};strcpy(old.station,"SLO");strcpy(old.direction,"east");strcpy(old.operators,"all");strcpy(old.refreshToken,"synthetic.opaque-token");old.intervalMinutes=1;old.updateStart=360;old.updateEnd=1320;
  RailConfig::Settings migrated;assert(RailConfig::migrate(old,migrated));assert(!strcmp(migrated.refreshToken,old.refreshToken));
  assert(!strcmp(migrated.station,"SLO")); // Saved stations survive a change to repository defaults.
  assert(migrated.schedule.count==0 && migrated.schedule.fallback.interval==1 && !strcmp(migrated.schedule.fallback.route,"east") && migrated.schedule.end==1320);
  old.updateStart=old.updateEnd;assert(RailConfig::migrate(old,migrated)&&migrated.schedule.allDay);
  old.refreshToken[0]=0;assert(RailConfig::migrate(old,migrated)); // Preserve unconfigured devices too.
  old.direction[7]='X';assert(!RailConfig::migrate(old,migrated));
  assert(sizeof(RailConfig::Stored)+128<=4096);
  puts("Schedule phase boundaries, overlaps, midnight, BST, budgeting, minimum spacing and legacy token/settings migration passed.");
}
