#pragma once
#include <ArduinoJson.h>
#include "rail_logic.h"

namespace RailBoard {
inline void makeFilter(JsonDocument& filter) {
  for (const char* key : {"generatedAt","crs","locationName","platformAvailable","areServicesAvailable"}) filter[key]=true;
  filter["nrccMessages"][0]["value"]=true; filter["nrccMessages"][0]["Value"]=true;
  for (const char* key : {"std","etd","isCancelled","operator","operatorCode","platform","cancelReason","delayReason"}) filter["trainServices"][0][key]=true;
  for (const char* key : {"origin","destination"}) {
    filter["trainServices"][0][key][0]["crs"]=true;
    filter["trainServices"][0][key][0]["locationName"]=true;
  }
}
struct Train {
  char destination[81], operatorName[29], operatorCode[4], scheduled[6], expected[6], platform[9], reason[181];
  RailLogic::Forecast forecast;
};
constexpr unsigned PREDICTED_LIMIT=8, UNCONFIRMED_LIMIT=2, CANCELLED_LIMIT=2;
constexpr unsigned UNCONFIRMED_OFFSET=PREDICTED_LIMIT, CANCELLED_OFFSET=PREDICTED_LIMIT+UNCONFIRMED_LIMIT;
constexpr unsigned CACHE_LIMIT=PREDICTED_LIMIT+UNCONFIRMED_LIMIT+CANCELLED_LIMIT;
struct Snapshot {
  char stationName[81], station[4], message[181];
  time_t generated;
  bool available,arrivals;
  unsigned count, runningCount;
  Train trains[CACHE_LIMIT];
};
inline bool upcoming(const Train& train, time_t now) {
  // An unknown estimate stays explicitly unconfirmed. Only a supplied ETA can
  // expire a running train; its scheduled time is not proof it has departed.
  return train.forecast.status != RailLogic::CANCELLED &&
    (!train.forecast.expected || train.forecast.expected >= now);
}
inline const Train* nextTrain(const Snapshot& board, time_t now, const Train* skip = nullptr, const Train* secondSkip = nullptr) {
  if (!board.available) return nullptr;
  for (unsigned i = 0; i < board.count; ++i)
    if (&board.trains[i] != skip && &board.trains[i] != secondSkip && upcoming(board.trains[i],now)) return &board.trains[i];
  return nullptr;
}
struct DisplayRows {const Train* primary=nullptr;const Train* following=nullptr;const Train* third=nullptr;};
inline bool rowUpcoming(const Train& train,time_t now) {
  return train.forecast.status==RailLogic::CANCELLED ? train.forecast.planned>=now : upcoming(train,now);
}
inline time_t rowTime(const Train& train) {return train.forecast.expected?train.forecast.expected:train.forecast.planned;}
inline DisplayRows displayRows(const Snapshot& board,time_t now) {
  DisplayRows rows;if(!board.available)return rows;
  rows.primary=nextTrain(board,now);
  const Train* cancellation=nullptr;
  for(unsigned i=0;i<board.count;++i) {
    const auto* train=&board.trains[i];
    if(train==rows.primary || !rowUpcoming(*train,now))continue;
    if(train->forecast.status==RailLogic::CANCELLED && (!cancellation || rowTime(*train)<rowTime(*cancellation)))cancellation=train;
    if(!rows.following || rowTime(*train)<rowTime(*rows.following)) {rows.third=rows.following;rows.following=train;}
    else if(!rows.third || rowTime(*train)<rowTime(*rows.third))rows.third=train;
  }
  // Keep the earliest cancellation visible, but never reserve a blank row
  // above it. Cancelled services cannot become the primary countdown.
  if(cancellation && cancellation!=rows.following && cancellation!=rows.third) {
    if(rows.following)rows.third=cancellation;else rows.following=cancellation;
  }
  return rows;
}
inline void copy(char* target, size_t capacity, const char* value) {
  size_t out = 0;
  if (value) for (size_t i = 0; value[i] && out+1 < capacity; ++i) {
    unsigned char c = value[i];
    if (c >= 32 && c <= 126) target[out++] = char(c);
    else if (c == '\n' || c == '\r' || c == '\t') target[out++] = ' ';
  }
  target[out] = 0;
}
inline bool operatorAllowed(const char* choice, const char* code) {
  return !strcmp(choice,"any") || (!strcmp(code,"GW") && (!strcmp(choice,"all") || !strcmp(choice,"gwr"))) ||
    (!strcmp(code,"XR") && (!strcmp(choice,"all") || !strcmp(choice,"elizabeth")));
}
inline RailLogic::Direction serviceDirection(const char* station, JsonObjectConst service, bool arrivals=false) {
  if(arrivals) {
    for(JsonObjectConst stop:service["origin"].as<JsonArrayConst>()) {
      auto dir=RailLogic::towards(station,stop["crs"] | "");if(dir!=RailLogic::UNKNOWN)return dir;
    }
    for(JsonObjectConst stop:service["destination"].as<JsonArrayConst>()) {
      auto dir=RailLogic::fromOrigin(station,stop["crs"] | "");if(dir!=RailLogic::UNKNOWN)return dir;
    }
    return RailLogic::UNKNOWN;
  }
  for (JsonObjectConst group : service["subsequentCallingPoints"].as<JsonArrayConst>())
    for (JsonObjectConst stop : group["callingPoint"].as<JsonArrayConst>()) {
      auto dir = RailLogic::towards(station,stop["crs"] | "");
      if (dir != RailLogic::UNKNOWN) return dir;
    }
  for (JsonObjectConst stop : service["destination"].as<JsonArrayConst>()) {
    auto dir = RailLogic::towards(station,stop["crs"] | "");
    if (dir != RailLogic::UNKNOWN) return dir;
  }
  for (JsonObjectConst stop : service["origin"].as<JsonArrayConst>()) {
    auto dir = RailLogic::fromOrigin(station,stop["crs"] | "");
    if (dir != RailLogic::UNKNOWN) return dir;
  }
  return RailLogic::UNKNOWN;
}
inline bool matches(const char* station, const char* choice, JsonObjectConst service, bool arrivals=false) {
  if (!strcmp(choice,"both")) return true;
  if (!strncmp(choice,"to-",3)) {
    for (JsonObjectConst stop : service[arrivals?"origin":"destination"].as<JsonArrayConst>())
      if (!strcmp(choice+3,stop["crs"] | "")) return true;
    return false;
  }
  auto dir = serviceDirection(station,service,arrivals);
  return (!strcmp(choice,"east") && dir == RailLogic::EAST) || (!strcmp(choice,"west") && dir == RailLogic::WEST) || (!strcmp(choice,"other") && dir == RailLogic::UNKNOWN);
}
inline void insert(Train* list, unsigned& count, unsigned limit, const Train& train, bool predicted) {
  time_t key = predicted ? train.forecast.expected : train.forecast.planned;
  unsigned pos = 0;
  while (pos < count && (predicted ? list[pos].forecast.expected : list[pos].forecast.planned) <= key) ++pos;
  if (pos >= limit) return;
  if (count < limit) ++count;
  for (unsigned i = count-1; i > pos; --i) list[i] = list[i-1];
  list[pos] = train;
}
inline bool parse(JsonObjectConst source, const char* station, const char* direction, const char* operators, time_t now, Snapshot& result, const char*& error, bool arrivals=false) {
  if (strcmp(source["crs"] | "",station)) { error = "Rail feed returned a different station"; return false; }
  time_t generated = RailLogic::isoTime(source["generatedAt"] | "");
  if (RailLogic::stale(now,generated)) { error = "Rail feed timestamp is missing or stale"; return false; }
  memset(&result,0,sizeof(result));
  result.generated=generated;result.arrivals=arrivals;
  copy(result.station, sizeof(result.station),station);
  copy(result.stationName,sizeof(result.stationName),source["locationName"] | station);
  result.available = source["areServicesAvailable"] | true;
  // Strip HTML from station-wide messages instead of drawing markup on the TFT.
  const char* raw = source["nrccMessages"][0]["value"] | "";
  if (!raw[0]) raw = source["nrccMessages"][0]["Value"] | "";
  bool tag = false; size_t pos = 0;
  for (size_t i = 0; raw[i] && pos+1 < sizeof(result.message); ++i) {
    if (raw[i] == '<') { tag = true; continue; }
    if (raw[i] == '>') { tag = false; continue; }
    if (!tag && raw[i] >= 32 && raw[i] <= 126) result.message[pos++] = raw[i];
  }
  result.message[pos] = 0;
  if (!result.available) return true;
  unsigned nr = 0, nu = 0, nc = 0;
  for (JsonObjectConst service : source["trainServices"].as<JsonArrayConst>()) {
    const char* code = service["operatorCode"] | "";
    if (!operatorAllowed(operators,code) || !matches(station,direction,service,arrivals)) continue;
    Train train = {};
    train.forecast = RailLogic::forecast(service[arrivals?"sta":"std"] | "",service[arrivals?"eta":"etd"] | "",service["isCancelled"] | false,result.generated);
    if (!train.forecast.planned) continue;
    if (train.forecast.expected && train.forecast.expected < now) continue;
    if (train.forecast.status==RailLogic::CANCELLED && train.forecast.planned<now) continue;
    copy(train.destination,sizeof(train.destination),service[arrivals?"origin":"destination"][0]["locationName"] | (arrivals?"Origin unavailable":"Destination unavailable"));
    if (!strcmp(train.destination,"London Paddington")) copy(train.destination,sizeof(train.destination),"Paddington");
    copy(train.operatorName,sizeof(train.operatorName),!strcmp(code,"GW") ? "GWR" : !strcmp(code,"XR") ? "Elizabeth line" : service["operator"] | "Rail service");
    copy(train.operatorCode,sizeof(train.operatorCode),code);
    copy(train.scheduled,sizeof(train.scheduled),service[arrivals?"sta":"std"] | "");
    if (train.forecast.expected) {
      struct tm local; localtime_r(&train.forecast.expected,&local);
      strftime(train.expected,sizeof(train.expected),"%H:%M",&local);
    }
    if (source["platformAvailable"] | true) copy(train.platform,sizeof(train.platform),service["platform"] | "");
    copy(train.reason,sizeof(train.reason),service[train.forecast.status == RailLogic::CANCELLED ? "cancelReason" : "delayReason"] | "");
    if(!RailLogic::usefulReason(train.reason))train.reason[0]=0;
    if (train.forecast.status == RailLogic::CANCELLED) insert(result.trains+CANCELLED_OFFSET,nc,CANCELLED_LIMIT,train,false);
    else if (train.forecast.expected) insert(result.trains,nr,PREDICTED_LIMIT,train,true);
    else insert(result.trains+UNCONFIRMED_OFFSET,nu,UNCONFIRMED_LIMIT,train,false);
  }
  result.runningCount = nr;
  result.count=nr;
  for (unsigned i = 0; i < nu; ++i) result.trains[result.count++] = result.trains[UNCONFIRMED_OFFSET+i];
  for (unsigned i = 0; i < nc; ++i) result.trains[result.count++] = result.trains[CANCELLED_OFFSET+i];
  return true;
}
}
