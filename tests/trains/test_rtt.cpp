#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "../../firmware/esp_train_departures/rtt_board.h"
#include "../../firmware/esp_train_departures/rtt_policy.h"
using namespace RttBoard;
struct Input {
  std::istringstream stream;
  explicit Input(const std::string& data):stream(data) {}
  int read(){return stream.get();}
  size_t readBytes(char* out,size_t length){stream.read(out,length);return size_t(stream.gcount());}
};
int main() {
  setenv("TZ","GMT0BST,M3.5.0/1,M10.5.0",1);tzset();
  std::ifstream file("examples/rtt-departures.json");assert(file.good());
  const std::string raw((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  time_t now=RailLogic::isoTime("2026-10-08T09:42:00Z");
  DynamicJsonDocument filter(1536),work(2048),fixture(16000);
  serviceFilter(filter);assert(!filter.overflowed());
  assert(filter["destination"][0]["location"]["shortCodes"][0].as<bool>());
  assert(!deserializeJson(fixture,raw));
  RailBoard::Snapshot board;const char* error=nullptr;
  auto parse=[&](const std::string& json,const char* dir="both",const char* operators="all") {
    Input input(json);return decode(input,filter,work,"PAD",dir,operators,now,board,error);
  };
  auto updated=[&]() {std::string out;serializeJson(fixture,out);return out;};
  assert(parse(raw));assert(board.count==5 && board.runningCount==3);
  assert(!strcmp(board.stationName,"London Paddington"));
  assert(!strcmp(board.trains[0].destination,"Abbey Wood"));
  assert(board.trains[1].forecast.delay==18); // Planned time already passed; prediction is still future.
  assert(!strcmp(board.trains[1].platform,"4"));
  assert(board.trains[3].forecast.status==RailLogic::DELAYED && !board.trains[3].forecast.expected);
  assert(board.trains[4].forecast.status==RailLogic::CANCELLED && !strcmp(board.trains[4].reason,"Signalling fault"));
  // Advance the cached board without another decode or request. Keep DUE at
  // the exact ETA, then promote immediately, using forecasts rather than STD.
  const auto* primary=RailBoard::nextTrain(board,now);
  assert(primary==&board.trains[0]);
  assert(RailBoard::nextTrain(board,now,primary)==&board.trains[1]);
  assert(RailBoard::nextTrain(board,now,primary,&board.trains[1])==&board.trains[2]);
  assert(RailBoard::nextTrain(board,primary->forecast.expected)==primary);
  assert(RailBoard::nextTrain(board,primary->forecast.expected+1)==&board.trains[1]);
  assert(RailBoard::nextTrain(board,board.trains[1].forecast.expected+1)==&board.trains[2]);
  assert(RailBoard::nextTrain(board,board.trains[2].forecast.expected+1)==&board.trains[3]);
  assert(RailBoard::upcoming(board.trains[1],now)); // Delayed train's STD is past.
  assert(!RailBoard::upcoming(board.trains[4],now));
  {
    RailBoard::Snapshot cached=board;
    cached.count=3;
    assert(!RailBoard::nextTrain(cached,cached.trains[2].forecast.expected+1));
    cached.available=false;
    assert(!RailBoard::nextTrain(cached,now));
    cached.available=true;cached.count=2;
    time_t midnight=RailLogic::isoTime("2026-10-08T23:59:00+01:00");
    cached.trains[0].forecast=RailLogic::forecast("23:59","On time",false,midnight);
    cached.trains[1].forecast=RailLogic::forecast("00:02","On time",false,midnight);
    assert(RailBoard::nextTrain(cached,midnight+1)==&cached.trains[1]);
  }
  assert(parse(raw,"east") && board.count==1);
  assert(parse(raw,"west") && board.count==4);
  assert(parse(raw,"both","elizabeth") && board.count==2);
  // Intermediate calling-point filtering is performed by the server.
  assert(parse(raw,"to-TWY") && board.count==5);
  fixture["services"][1]["temporalData"]["departure"]["realtimeNoReport"]=true;
  assert(parse(updated()) && !board.trains[3].forecast.expected);
  fixture["services"][1]["temporalData"]["departure"]["realtimeEstimate"]="2026-10-08T10:55:00+01:00";
  assert(parse(updated()) && board.runningCount==3);
  fixture["services"][1]["temporalData"]["departure"]["realtimeActual"]="2026-10-08T10:41:00+01:00";
  assert(parse(updated()) && board.count==4);
  fixture["systemStatus"]["rttCore"]="SCHEDULE_ONLY";
  assert(parse(updated()) && board.message[0]);
  for(unsigned i=0;i<board.count;++i)assert(!board.trains[i].forecast.expected);
  fixture["query"]["location"]["shortCodes"][0]="RDG";
  assert(!parse(updated()));
  assert(!parse(raw.substr(0,raw.size()-3)));
  assert(!parse("{\"services\":[]}"));
  assert(!parse("{\"services\":[{},]}"));
  assert(!deserializeJson(fixture,raw));
  fixture["services"][0]["locationMetadata"]["platform"]["actual"]="5";
  fixture["services"][1]["temporalData"]["displayAs"]="PASS";
  assert(parse(updated()) && board.runningCount==2 && !strcmp(board.trains[0].platform,"5"));
  // Observed live shape: origin/destination have descriptions only, and train
  // timestamps are UK local times without offsets. Direction must still work.
  assert(!deserializeJson(fixture,raw));
  for(JsonObject service:fixture["services"].as<JsonArray>()) {
    for(const char* field:{"origin","destination"})for(JsonObject pair:service[field].as<JsonArray>()) {
      pair["location"].remove("shortCodes");pair["location"].remove("namespace");
    }
    for(const char* field:{"scheduleAdvertised","realtimeForecast"}) {
      const char* value=service["temporalData"]["departure"][field] | "";
      if(*value)service["temporalData"]["departure"][field]=std::string(value,19);
    }
  }
  assert(parse(updated(),"east") && board.count==1 && board.trains[0].forecast.expected==now+9*60);
  assert(parse(updated(),"west") && board.count==4);
  assert(rttTime("2026-10-08T10:42:00")==now);
  assert(rttTime("2026-10-08T10:42:00.123")==now);
  assert(rttTime("2026-12-01T10:42:00")==RailLogic::isoTime("2026-12-01T10:42:00Z"));
  assert(!rttTime("2026-03-29T01:30:00") && !rttTime("2026-02-30T10:42:00"));
  fixture["services"][0]["destination"][0]["location"]["description"]="Somewhere unknown";
  fixture["services"][0]["origin"][0]["location"]["description"]="Elsewhere unknown";
  assert(directionOf("PAD",fixture["services"][0])==RailLogic::UNKNOWN);
  // Basic responses may omit optional metadata. Public boarding calls survive;
  // explicit non-passenger and non-train services remain excluded.
  assert(!deserializeJson(fixture,raw));
  for(JsonObject service:fixture["services"].as<JsonArray>()) {
    service["scheduleMetadata"].remove("inPassengerService");service["scheduleMetadata"].remove("modeType");
  }
  assert(parse(updated()) && board.count==5);
  fixture["services"][0]["scheduleMetadata"]["inPassengerService"]=false;
  assert(parse(updated()) && board.count==4);
  fixture["services"][1]["scheduleMetadata"]["modeType"]="REPLACEMENT_BUS";
  assert(parse(updated()) && board.count==3);
  assert(!deserializeJson(fixture,raw));
  {Input input(raw);Diagnostics diagnostics;
   assert(decode(input,filter,work,"PAD","east","all",now,board,error,&diagnostics));
   assert(diagnostics.seen==5 && diagnostics.directionFiltered==4 && board.count==1 && diagnostics.sample[0]);}
  // Arrival boards use arrival forecasts, origins and set-down/terminating
  // calls. A train that already arrived must not survive via its departure ETA.
  assert(!deserializeJson(fixture,raw));
  for(JsonObject service:fixture["services"].as<JsonArray>()) {
    service["temporalData"]["arrival"].set(service["temporalData"]["departure"]);
    for(const char* field:{"scheduleAdvertised","realtimeForecast"}) {
      const char* value=service["temporalData"]["arrival"][field] | "";
      time_t t=rttTime(value);if(t){t-=60;struct tm local;localtime_r(&t,&local);char label[32];strftime(label,sizeof(label),"%Y-%m-%dT%H:%M:%S",&local);service["temporalData"]["arrival"][field]=label;}
    }
  }
  auto arrivals=[&](const char* route="both") {Input input(updated());return decode(input,filter,work,"PAD",route,"all",now,board,error,nullptr,true);};
  assert(arrivals() && board.arrivals && board.count==5);
  assert(!strcmp(board.trains[0].destination,"Reading") && board.trains[0].forecast.expected==now+8*60);
  assert(arrivals("west") && board.count==2);assert(arrivals("east") && board.count==3);
  fixture["services"][1]["temporalData"]["departure"]["realtimeActual"]="2026-10-08T10:41:00";
  assert(arrivals("west") && board.count==2); // Departure actual is irrelevant to arrival decoding.
  fixture["services"][1]["temporalData"]["arrival"]["realtimeActual"]="2026-10-08T10:41:00";
  assert(arrivals("west") && board.count==1);
  fixture["services"][1]["temporalData"]["arrival"].remove("realtimeActual");
  fixture["services"][1]["temporalData"]["scheduledCallType"]="ADVERTISED_SET_DOWN";
  fixture["services"][1]["temporalData"]["realtimeCallType"]="ADVERTISED_SET_DOWN";
  fixture["services"][1]["temporalData"]["displayAs"]="TERMINATES";
  assert(arrivals("west") && board.count==2);
  fixture["services"][1]["temporalData"]["displayAs"]="STARTS";
  assert(arrivals("west") && board.count==1);
  fixture["services"][1]["temporalData"]["displayAs"]="CALL";
  fixture["services"][1]["temporalData"]["realtimeCallType"]="ADVERTISED_PICK_UP";
  assert(arrivals("west") && board.count==1);
  fixture["services"][1]["temporalData"]["realtimeCallType"]="ADVERTISED_SET_DOWN";
  fixture["services"][1]["temporalData"]["arrival"]["isCancelled"]=true;
  assert(arrivals("west") && board.count==2 && board.trains[1].forecast.status==RailLogic::CANCELLED);
  fixture["services"][1]["temporalData"]["arrival"].remove("isCancelled");
  fixture["services"][1]["temporalData"]["arrival"].remove("realtimeForecast");
  assert(arrivals("west") && !board.trains[1].forecast.expected);
  assert(!deserializeJson(fixture,raw));
  // Hundreds of trains fit the same fixed per-service JSON capacity.
  std::string train;serializeJson(fixture["services"][0],train);
  fixture["services"].as<JsonArray>().clear();std::string stress=updated();
  auto at=stress.find("\"services\":[]");assert(at!=std::string::npos);
  std::string list="\"services\":[";
  for(unsigned i=0;i<200;++i){if(i)list+=",";list+=train;}list+="]";
  stress.replace(at,13,list);
  assert(parse(stress) && board.count==3);
  assert(!RailLogic::stale(now+629,now,630) && RailLogic::stale(now+631,now,630));
  assert(RttPolicy::dailyRequests(360,1380,1)==1020 && RttPolicy::dailyRequests(360,1380,2)==510);
  assert(RttPolicy::dailyRequests(360,1320,1)==960 && RttPolicy::dailyRequests(360,360,1)==1440);
  assert(RttPolicy::dailyRequests(1380,360,1)==420);
  assert(RttPolicy::quietHours(RailLogic::isoTime("2026-10-08T12:00:00+01:00"),1380,360));
  assert(!RttPolicy::quietHours(RailLogic::isoTime("2026-10-08T23:00:00+01:00"),1380,360));
  assert(!RttPolicy::quietHours(RailLogic::isoTime("2026-10-08T12:00:00+01:00"),360,360));
  assert(RttPolicy::validInterval(2) && RttPolicy::validInterval(10) && RttPolicy::validInterval(1) && !RttPolicy::validInterval(4));
  assert(!RttPolicy::quietHours(RailLogic::isoTime("2026-10-08T22:59:59+01:00")));
  assert(RttPolicy::quietHours(RailLogic::isoTime("2026-10-08T23:00:00+01:00")));
  assert(RttPolicy::quietHours(RailLogic::isoTime("2026-10-09T05:59:59+01:00")));
  assert(!RttPolicy::quietHours(RailLogic::isoTime("2026-10-09T06:00:00+01:00")));
  assert(RttPolicy::quietHours(RailLogic::isoTime("2026-12-01T23:00:00Z")));
  assert(!RttPolicy::quietHours(RailLogic::isoTime("2026-12-02T06:00:00Z")));
  assert(!RttPolicy::quietHours(0));
  assert(RttPolicy::validToken("synthetic.opaque-token_123"));
  assert(!RttPolicy::validToken("secret\r\nInjected: header") && !RttPolicy::validToken("has space"));
  assert(RttPolicy::retrySeconds("60")==180 && RttPolicy::retrySeconds("3600")==3600);
  assert(RttPolicy::retrySeconds("999999999999999999")==604800 && RttPolicy::retrySeconds("bad")==180);
  assert(RttPolicy::due(10,0xfffffff0) && !RttPolicy::due(0xfffffff0,10));
  puts("RTT streaming, cached departure promotion, direction, delay, missing estimates, cancellations, degraded feeds, quotas and UK quiet-hours checks passed.");
}
