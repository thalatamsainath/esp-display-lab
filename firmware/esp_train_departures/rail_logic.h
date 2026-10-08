#pragma once
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace RailLogic {
constexpr uint32_t STALE_SECONDS = 180;
struct Station { const char* crs; const char* name; };
static const Station stations[] = {
  {"DID","Didcot Parkway"}, {"CHO","Cholsey"}, {"GOR","Goring & Streatley"},
  {"PAN","Pangbourne"}, {"TLH","Tilehurst"}, {"RDG","Reading"}, {"TWY","Twyford"},
  {"MAI","Maidenhead"}, {"TAP","Taplow"}, {"BNM","Burnham"}, {"SLO","Slough"},
  {"LNY","Langley"}, {"IVR","Iver"}, {"WDT","West Drayton"}, {"HAY","Hayes & Harlington"},
  {"STL","Southall"}, {"HAN","Hanwell"}, {"WEA","West Ealing"}, {"EAL","Ealing Broadway"},
  {"AML","Acton Main Line"}, {"PAD","London Paddington"}, {"BDS","Bond Street"},
  {"TCR","Tottenham Court Road"}, {"ZFD","Farringdon"}, {"LST","London Liverpool Street"},
  {"ZLW","Whitechapel"}, {"CWX","Canary Wharf"}, {"CUS","Custom House"},
  {"WWC","Woolwich"}, {"ABW","Abbey Wood"}
};
constexpr unsigned STATION_COUNT = sizeof(stations) / sizeof(stations[0]);
inline bool crs(const char* code) {
  return code && strlen(code) == 3 && code[0] >= 'A' && code[0] <= 'Z' && code[1] >= 'A' && code[1] <= 'Z' && code[2] >= 'A' && code[2] <= 'Z';
}
inline int position(const char* code) {
  if (!code) return -1;
  for (unsigned i = 0; i < STATION_COUNT; ++i) if (!strcmp(stations[i].crs, code)) return i;
  return -1;
}
inline int anchorPosition(const char* code) {
  // Heathrow branches join the main corridor at Hayes & Harlington.
  if (code && (!strcmp(code,"HXX") || !strcmp(code,"HAF") || !strcmp(code,"HWV"))) return position("HAY");
  return position(code);
}
enum Direction { UNKNOWN, EAST, WEST };
inline Direction towards(const char* station, const char* destination) {
  int from = position(station), to = anchorPosition(destination);
  if (from < 0 || to < 0 || from == to) return UNKNOWN;
  return to > from ? EAST : WEST;
}
inline Direction fromOrigin(const char* station, const char* origin) {
  Direction opposite = towards(station, origin);
  return opposite == EAST ? WEST : opposite == WEST ? EAST : UNKNOWN;
}
inline bool directionValid(const char* station, const char* direction) {
  if (!strcmp(direction,"both")) return true;
  if (!strncmp(direction,"to-",3)) return crs(direction + 3) && strcmp(station,direction + 3);
  int pos = position(station);
  if (pos < 0) return false;
  return (!strcmp(direction,"east") && pos < int(STATION_COUNT - 1)) ||
         (!strcmp(direction,"west") && pos > 0) || !strcmp(direction,"other");
}
inline bool clock(const char* value, int& hour, int& minute) {
  if (!value || strlen(value) != 5 || value[2] != ':' || !isdigit(value[0]) || !isdigit(value[1]) || !isdigit(value[3]) || !isdigit(value[4])) return false;
  hour = (value[0]-'0')*10 + value[1]-'0'; minute = (value[3]-'0')*10 + value[4]-'0';
  return hour < 24 && minute < 60;
}
inline time_t nearClock(const char* value, time_t reference) {
  int h, m; if (!clock(value,h,m)) return 0;
  struct tm local; localtime_r(&reference,&local);
  time_t best = 0; int64_t distance = INT64_MAX;
  for (int offset = -1; offset <= 1; ++offset) {
    struct tm candidate = local; candidate.tm_mday += offset; candidate.tm_hour = h;
    candidate.tm_min = m; candidate.tm_sec = 0; candidate.tm_isdst = -1;
    time_t epoch = mktime(&candidate);
    int64_t delta = int64_t(epoch) - reference; if (delta < 0) delta = -delta;
    if (delta < distance) { distance = delta; best = epoch; }
  }
  return best;
}
inline time_t isoTime(const char* value) {
  int y, mo, d, h, mi, s;
  if (!value || strlen(value) < 20 || sscanf(value,"%d-%d-%dT%d:%d:%d",&y,&mo,&d,&h,&mi,&s) != 6 ||
      y < 2020 || y > 2037 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59 || h < 0 || mi < 0 || s < 0) return 0;
  static const int monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap=(y%4==0 && (y%100!=0 || y%400==0));
  if(d>monthDays[mo-1]+(mo==2 && leap?1:0)) return 0;
  const char* zone = value + 19;
  if (*zone == '.') { ++zone; while (isdigit(*zone)) ++zone; }
  int offset = 0;
  if (*zone != 'Z' && *zone != 'z') {
    int zh, zm;
    if ((*zone != '+' && *zone != '-') || !clock(zone + 1, zh, zm)) return 0;
    offset = (zh*60 + zm)*60 * (*zone == '-' ? -1 : 1);
  } else if (zone[1]) return 0;
  // Gregorian civil-date conversion, independent of the configured local TZ.
  y -= mo <= 2; int era = y / 400; unsigned yo = y - era*400;
  unsigned doy = (153*(mo + (mo > 2 ? -3 : 9))+2)/5 + d-1;
  unsigned doe = yo*365 + yo/4 - yo/100 + doy;
  int64_t days = int64_t(era)*146097 + doe - 719468;
  return time_t(days*86400 + h*3600 + mi*60 + s - offset);
}
enum Status { ON_TIME, DELAYED, CANCELLED, UNCONFIRMED };
struct Forecast { Status status; time_t planned; time_t expected; int delay; };
inline Forecast forecast(const char* planned, const char* estimate, bool cancelled, time_t reference) {
  Forecast result = {UNCONFIRMED, nearClock(planned,reference), 0, -1};
  if (cancelled || (estimate && !strcmp(estimate,"Cancelled"))) { result.status = CANCELLED; return result; }
  if (!result.planned) return result;
  if (estimate && !strcmp(estimate,"On time")) result.expected = result.planned;
  else result.expected = nearClock(estimate, result.planned);
  if (!result.expected) {
    if (estimate && !strcmp(estimate,"Delayed")) result.status = DELAYED;
    return result;
  }
  result.delay = int((result.expected-result.planned)/60);
  result.status = result.delay > 0 ? DELAYED : ON_TIME;
  if (result.delay < 0) result.delay = 0;
  return result;
}
inline bool stale(time_t now, time_t generated, uint32_t maxAge=STALE_SECONDS) { return !generated || now < generated-120 || now-generated > maxAge; }
inline int countdown(time_t expected, time_t now) {
  if (!expected) return -1;
  int64_t seconds = int64_t(expected)-now;
  return seconds <= 0 ? 0 : int((seconds+59)/60);
}
inline unsigned backlightDuty(unsigned percent) { return 1023u - (percent*1023u+50u)/100u; }
}
