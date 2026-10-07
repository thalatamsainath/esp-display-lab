#include <cassert>
#include <initializer_list>
#include <cmath>
#include <cstdint>
#include "../../firmware/esp_synology_display/nas_logic.h"

int main() {
  double value;
  assert(NasLogic::number(" 1.25 ", value) && value == 1.25);
  for (const char* invalid : {"", "--", "nan", "inf", "12MB/s", "20oops"}) assert(!NasLogic::number(invalid, value));
  assert(NasLogic::percent(5.4, 8) == 68);
  assert(NasLogic::percent(1.2, 4) == 30);
  assert(NasLogic::percent(0, 8) == 0);
  assert(NasLogic::percent(8, 8) == 100);
  assert(NasLogic::percent(1, 0) == -1);
  assert(NasLogic::percent(9, 8) == -1);
  auto avg = NasLogic::average("38,36,40,34");
  assert(avg.count == 4 && avg.mean == 37);
  avg = NasLogic::average("38,unavailable,36,0,-1,nan,126");
  assert(avg.count == 2 && avg.mean == 37);
  assert(NasLogic::average("unknown,,0").count == 0);
  // Average physical drive readings, not two volume averages with unequal counts.
  avg = NasLogic::average("30,40,50");
  assert(avg.mean == 40);
  for (const char* valid : {"normal", "Normal", "healthy", "DataScrubbing", "repairing"}) assert(NasLogic::normalOrMaintenance(valid));
  for (const char* fault : {"degraded", "crashed / read-only", "expansion interrupted", "unknown", "", "unsupported"}) assert(!NasLogic::normalOrMaintenance(fault));
  assert(!NasLogic::stale(179999, 0, 180000));
  assert(NasLogic::stale(180000, 0, 180000));
  assert(!NasLogic::stale(100, UINT32_MAX - 100, 180000));
  assert(NasLogic::stale(180100, UINT32_MAX - 100, 180000));
  unsigned brightness;
  assert(NasLogic::brightness("0", brightness) && brightness == 0);
  assert(NasLogic::brightness("100", brightness) && brightness == 100);
  assert(NasLogic::brightness(" 37 ", brightness) && brightness == 37);
  for (const char* invalid : {"", "nan", "-1", "101", "33.5", "50%"}) assert(!NasLogic::brightness(invalid, brightness));
  assert(NasLogic::backlightDuty(0) == 1023);
  assert(NasLogic::backlightDuty(100) == 0);
  for (unsigned percent = 1; percent <= 100; ++percent) {
    assert(NasLogic::backlightDuty(percent) < NasLogic::backlightDuty(percent - 1));
  }
}
