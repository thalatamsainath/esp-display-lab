#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>
namespace RttPolicy {
constexpr uint32_t MAX_WAIT_SECONDS=604800;
inline bool validInterval(unsigned minutes) {return minutes==1 || minutes==2 || minutes==3 || minutes==5 || minutes==10;}
inline unsigned activeMinutes(unsigned start,unsigned end) {return start==end?1440:(end+1440-start)%1440;}
inline unsigned dailyRequests(unsigned start,unsigned end,unsigned interval) {
  return interval?(activeMinutes(start,end)+interval-1)/interval:0;
}
inline bool quietHours(time_t now,unsigned start=360,unsigned end=1380) {
  if(now<1700000000 || start==end)return false;
  struct tm local;localtime_r(&now,&local);unsigned minute=local.tm_hour*60+local.tm_min;
  bool active=start<end?minute>=start && minute<end:minute>=start || minute<end;
  return !active;
}
inline bool validToken(const char* token) {
  if(!token || !*token)return false;
  for(size_t i=0;token[i];++i)if(token[i]<=32 || token[i]>=127)return false;
  return true;
}
inline uint32_t retrySeconds(const char* text) {
  if(!text || !*text)return 180;
  uint32_t value=0;
  for(size_t i=0;text[i];++i) {
    if(text[i]<'0' || text[i]>'9')return 180;
    value=value*10+text[i]-'0';
    if(value>=MAX_WAIT_SECONDS)return MAX_WAIT_SECONDS;
  }
  return value<180?180:value;
}
inline bool due(uint32_t now,uint32_t deadline) { return int32_t(now-deadline)>=0; }
}
