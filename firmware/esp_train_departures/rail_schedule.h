#pragma once
#include "rail_logic.h"

namespace RailSchedule {
constexpr unsigned MAX_RULES=8;
enum Mode : uint8_t { DEPARTURES=0, ARRIVALS=1 };
struct Rule { uint16_t start,end; uint8_t interval,mode; char route[8]; };
struct Schedule { uint16_t start,end; uint8_t allDay,count; Rule fallback,rules[MAX_RULES]; };
struct Selection { int index; bool paused; };
inline bool validInterval(unsigned n) { return n>=1 && n<=60; }
inline bool inside(unsigned minute,unsigned start,unsigned end) {
  return start==end || (start<end ? minute>=start && minute<end : minute>=start || minute<end);
}
inline Selection at(const Schedule& s,unsigned minute) {
  if(!s.allDay && !inside(minute,s.start,s.end))return {-2,true};
  for(unsigned i=0;i<s.count;++i)if(inside(minute,s.rules[i].start,s.rules[i].end))return {int(i),false};
  return {-1,false};
}
inline Selection at(const Schedule& s,time_t now) {
  if(now<1700000000)return {-1,false};
  struct tm local;localtime_r(&now,&local);
  return at(s,unsigned(local.tm_hour*60+local.tm_min));
}
inline const Rule& rule(const Schedule& s,int index) {return index>=0?s.rules[index]:s.fallback;}
inline const char* modeName(unsigned mode) {return mode==ARRIVALS?"arrivals":"departures";}
inline bool sameBoard(const Rule& a,const Rule& b) {return a.mode==b.mode && !strcmp(a.route,b.route);}
inline bool validRule(const Rule& r,const char* station) {
  return validInterval(r.interval) && r.mode<=ARRIVALS && r.route[7]==0 && RailLogic::directionValid(station,r.route);
}
inline bool valid(const Schedule& s,const char* station,const char*& error) {
  if(s.start>=1440 || s.end>=1440 || s.allDay>1 || (!s.allDay && s.start==s.end) || s.count>MAX_RULES) {error="Check active hours and number of windows";return false;}
  if(!validRule(s.fallback,station)) {error="Check default board, route and 1-60 minute interval";return false;}
  for(unsigned i=0;i<s.count;++i) {
    const auto& r=s.rules[i];
    if(r.start>=1440 || r.end>=1440 || r.start==r.end || !validRule(r,station)) {error="Check each time window, board, route and 1-60 minute interval";return false;}
    for(unsigned j=0;j<i;++j)
      if(inside(r.start,s.rules[j].start,s.rules[j].end) || inside(s.rules[j].start,r.start,r.end)) {error="Time windows overlap";return false;}
  }
  return true;
}
inline unsigned calls(const Schedule& s,int index,unsigned length) {
  if(index==-2 || !length)return 0;
  unsigned interval=rule(s,index).interval;
  return interval?(length+interval-1)/interval:0;
}
// Count contiguous windows, joining a window that crosses midnight. This is a
// nominal 24-hour UK day; DST changes can shorten or lengthen the actual day.
inline unsigned dailyRequests(const Schedule& s) {
  int first=at(s,0u).index,current=first;unsigned length=0,firstLength=0,total=0,segments=0;
  for(unsigned minute=0;minute<1440;++minute) {
    int key=at(s,minute).index;
    if(key!=current) {
      total+=calls(s,current,length);if(!segments)firstLength=length;
      ++segments;current=key;length=0;
    }
    ++length;
  }
  total+=calls(s,current,length);
  if(segments && first==current)total=total-calls(s,first,firstLength)-calls(s,current,length)+calls(s,first,firstLength+length);
  return total;
}
// A phase change queues a board refresh with at least one minute between
// attempts; RTT quota deadlines are enforced separately and never shortened.
inline bool requestDue(uint32_t now,uint32_t last,bool attempted,unsigned interval,bool pending) {
  return !attempted || now-last>=uint32_t(pending?1:interval)*60000u;
}
}
