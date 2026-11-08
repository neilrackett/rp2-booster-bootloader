/**
 * File: vercmp.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Version string comparison for the firmware update check
 */

#include "vercmp.h"

// Parse up to three numeric components from a version string (e.g.,
// "2.0.5alpha" -> {2,0,5})
static void parse_version3_numbers(const char *str, unsigned int out[3]) {
  out[0] = out[1] = out[2] = 0U;
  if (!str) return;
  const char *p = str;
  for (int k = 0; k < 3 && *p; ++k) {
    while (*p && (*p < '0' || *p > '9')) ++p;
    unsigned v = 0U;
    while (*p >= '0' && *p <= '9') {
      v = v * 10u + (unsigned)(*p - '0');
      ++p;
    }
    out[k] = v;
  }
}

bool vercmp_isNewer(const char *candidate, const char *current) {
  unsigned cur[3] = {0}, rel[3] = {0};
  parse_version3_numbers(candidate, cur);
  parse_version3_numbers(current, rel);
  for (int i = 0; i < 3; ++i) {
    if (cur[i] != rel[i]) return cur[i] > rel[i];
  }
  return false;
}
