#include <cassert>
#include <cstdlib>
#include <ctime>
#include "../../firmware/esp_train_departures/rail_logic.h"
using namespace RailLogic;
int main() {
  setenv("TZ","GMT0BST,M3.5.0/1,M10.5.0",1); tzset();
  assert(crs("PAD") && !crs("PAD4") && !crs("pad") && !crs("<x>"));
  assert(directionValid("PAD","east") && directionValid("PAD","west") && directionValid("PAD","both"));
  assert(!directionValid("PAD","to-PAD") && directionValid("PAD","to-RDG"));
  assert(!directionValid("ABW","east") && !directionValid("DID","west"));
  assert(!directionValid("ABC","east") && directionValid("ABC","both"));
  assert(towards("PAD","BDS")==EAST && towards("PAD","ABW")==EAST && towards("PAD","RDG")==WEST);
  assert(towards("RDG","HXX")==EAST && towards("PAD","HXX")==WEST);
  assert(fromOrigin("PAD","ABW")==WEST && fromOrigin("PAD","RDG")==EAST);
  time_t now=isoTime("2026-10-08T10:42:00+01:00");
  assert(now==isoTime("2026-10-08T09:42:00Z"));
  assert(isoTime("2026-10-08T09:42:00.123Z")==now);
  assert(!isoTime("2026-02-30T10:42:00Z"));
  assert(!isoTime("2026-10-08T09:42:00") && !isoTime("broken"));
  auto ontime=forecast("10:51","On time",false,now);
  assert(ontime.status==ON_TIME && countdown(ontime.expected,now)==9);
  auto delay=forecast("10:46","10:53",false,now);
  assert(delay.status==DELAYED && delay.delay==7 && countdown(delay.expected,now)==11);
  auto unknown=forecast("10:46","Delayed",false,now);
  assert(unknown.status==DELAYED && !unknown.expected && countdown(unknown.expected,now)==-1);
  assert(forecast("10:46","",false,now).status==UNCONFIRMED);
  auto cancelled=forecast("11:03","11:03",true,now);
  assert(cancelled.status==CANCELLED && !cancelled.expected);
  assert(!showDelayReason(ontime,"unknown cause"));
  assert(!showDelayReason(ontime,"Earlier signalling fault")); // A retained reason is not a current delay.
  assert(!showDelayReason(delay,"unknown cause"));
  assert(!showDelayReason(delay,"  UNKNOWN CAUSE.  "));
  assert(!showDelayReason(delay,"unknown reason") && !showDelayReason(delay,"unknown"));
  assert(!showDelayReason(delay,"") && !showDelayReason(delay,nullptr));
  assert(showDelayReason(delay,"Signalling fault"));
  assert(showDelayReason(unknown,"Waiting for a platform"));
  assert(!showDelayReason(cancelled,"Signalling fault"));
  assert(forecast("11:03","Cancelled",false,now).status==CANCELLED);
  assert(!forecast("25:99","On time",false,now).expected);
  time_t midnight=isoTime("2026-10-08T23:58:00+01:00");
  auto night=forecast("00:02","00:09",false,midnight);
  assert(night.delay==7 && countdown(night.expected,midnight)==11);
  time_t winter=isoTime("2026-12-01T10:42:00Z");
  assert(countdown(forecast("10:51","On time",false,winter).expected,winter)==9);
  assert(!stale(now,now-180) && stale(now,now-181) && stale(now,now+121));
  assert(countdown(now-20,now)==0 && countdown(now+1,now)==1);
  assert(backlightDuty(0)==1023 && backlightDuty(100)==0);
  puts("Train direction, delay, cancellation, midnight, timezone and stale-data checks passed.");
}
