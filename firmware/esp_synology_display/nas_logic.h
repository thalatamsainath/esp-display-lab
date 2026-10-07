#pragma once
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace NasLogic {
inline bool number(const char* text, double& value) {
  if (!text) return false;
  while (isspace(static_cast<unsigned char>(*text))) ++text;
  if (!*text) return false;
  char* end;
  value = strtod(text, &end);
  if (end == text || !isfinite(value)) return false;
  while (isspace(static_cast<unsigned char>(*end))) ++end;
  return !*end;
}

inline bool capacity(double used, double total) {
  return isfinite(used) && isfinite(total) && total > 0 && used >= 0 && used <= total;
}

inline int percent(double used, double total) {
  return capacity(used, total) ? static_cast<int>(lround(100.0 * used / total)) : -1;
}

struct Temperature { double mean; unsigned count; };
inline Temperature average(const char* csv) {
  Temperature result = {0, 0};
  if (!csv) return result;
  // Count each physical drive once; unavailable readings contribute nothing.
  while (*csv) {
    const char* comma = strchr(csv, ',');
    size_t length = comma ? static_cast<size_t>(comma - csv) : strlen(csv);
    char token[24];
    if (length < sizeof(token)) {
      memcpy(token, csv, length); token[length] = 0;
      double value;
      if (number(token, value) && value > 0 && value <= 125) {
        result.mean += value; ++result.count;
      }
    }
    if (!comma) break;
    csv = comma + 1;
  }
  if (result.count) result.mean /= result.count;
  return result;
}

inline bool normalOrMaintenance(const char* text) {
  char normalized[48]; size_t i = 0;
  if (!text) return false;
  while (*text && i < sizeof(normalized) - 1) {
    if (isalnum(static_cast<unsigned char>(*text))) normalized[i++] = tolower(static_cast<unsigned char>(*text));
    ++text;
  }
  normalized[i] = 0;
  const char* allowed[] = {"normal", "healthy", "ok", "repairing", "migrating", "expanding", "deleting", "creating", "raidsyncing", "raidparitychecking", "raidassembling", "canceling", "datascrubbing", "raiddeploying", "raidundeploying", "raidmountcache", "raidunmountcache", "raidconvertshrtopool", "raidmigrateshr1toshr2"};
  for (const char* status : allowed) if (!strcmp(normalized, status)) return true;
  return false;
}

inline bool stale(uint32_t now, uint32_t received, uint32_t timeout) {
  return static_cast<uint32_t>(now - received) >= timeout;
}

inline bool brightness(const char* text, unsigned& percent) {
  double value;
  if (!number(text, value) || value < 0 || value > 100 || floor(value) != value) return false;
  percent = static_cast<unsigned>(value);
  return true;
}

inline unsigned backlightDuty(unsigned percent) {
  // GPIO 5 is active LOW on this display: zero duty is full brightness.
  if (percent > 100) percent = 100;
  return ((100 - percent) * 1023 + 50) / 100;
}
}
