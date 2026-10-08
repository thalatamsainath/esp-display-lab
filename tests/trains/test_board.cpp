#include <cassert>
#include <cstdlib>
#include <fstream>
#include "../../firmware/esp_train_departures/rail_board.h"
using namespace RailBoard;
int main() {
  setenv("TZ","GMT0BST,M3.5.0/1,M10.5.0",1);tzset();
  std::ifstream fixture("examples/train-departures.json"); assert(fixture.good());
  StaticJsonDocument<2048> filter; makeFilter(filter); assert(!filter.overflowed());
  DynamicJsonDocument doc(12000); assert(!deserializeJson(doc,fixture,DeserializationOption::Filter(filter)));
  time_t now=RailLogic::isoTime("2026-10-08T09:42:00Z");
  Snapshot board; const char* error=nullptr;
  auto source=doc.as<JsonObjectConst>();
  assert(parse(source,"PAD","both","all",now,board,error));
  assert(board.runningCount==3 && board.count==5);
  assert(!strcmp(board.trains[0].destination,"Abbey Wood"));
  assert(board.trains[1].forecast.delay==7);
  assert(board.trains[3].forecast.status==RailLogic::DELAYED && !board.trains[3].forecast.expected);
  assert(board.trains[4].forecast.status==RailLogic::CANCELLED && !strcmp(board.trains[4].reason,"Signalling fault"));
  assert(parse(source,"PAD","east","all",now,board,error) && board.count==1);
  assert(parse(source,"PAD","west","all",now,board,error) && board.count==4);
  assert(parse(source,"PAD","both","elizabeth",now,board,error) && board.count==2);
  assert(parse(source,"PAD","to-RDG","all",now,board,error) && board.count==4);
  assert(parse(source,"PAD","both","any",now,board,error) && board.count==5); // Three earliest running predictions retained.
  assert(!parse(source,"RDG","both","all",now,board,error));
  assert(!parse(source,"PAD","both","all",now+181,board,error));
  doc["platformAvailable"]=false;
  assert(parse(source,"PAD","both","all",now,board,error));
  assert(!board.trains[0].platform[0]);
  doc["areServicesAvailable"]=false;
  assert(parse(source,"PAD","both","all",now,board,error) && !board.available && !board.count);
  doc["areServicesAvailable"]=true;
  doc["trainServices"][0]["destination"][0]["crs"]="OXF";
  doc["trainServices"][0]["origin"][0]["crs"]="ABW";
  assert(serviceDirection("PAD",doc["trainServices"][0])==RailLogic::WEST);
  puts("Rail board filtering, next-service ordering, unknown ETAs, faults and cancellation checks passed.");
}
