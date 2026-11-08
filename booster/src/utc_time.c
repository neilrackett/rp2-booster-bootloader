/**
 * File: utc_time.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Calendar time in UTC, without newlib's time-zone code
 *
 * Booster sets no time zone, so newlib's localtime_r() and mktime() already
 * work in UTC. But they parse the TZ variable on every call, and that parser
 * uses sscanf(), which in the Arm GNU toolchains' newlib (built with
 * multibyte support) links about 27 KB of Unicode classification tables.
 * Ubuntu's newlib, which CI and the releases use, has no such tables, so the
 * same firmware was 26 KB bigger built on a Mac and a debug build did not fit
 * its slot. The device build defines localtime_r() and mktime() here, in
 * UTC, so the linker never takes newlib's: FatFs's file timestamps
 * (my_rtc.c), its crash report (crash.c) and the SDK's datetime helpers all
 * call them.
 */

#include "utc_time.h"

#include <stdint.h>

// NOLINTBEGIN(readability-magic-numbers)
enum {
  SECONDS_PER_DAY = 86400,
  DAYS_PER_ERA = 146097,  // 400 Gregorian years
  DAYS_TO_1970 = 719468,  // 0000-03-01 to 1970-01-01
  MONTHS_PER_YEAR = 12,
  TM_YEAR_BASE = 1900,
};

// Days since 1970-01-01 of a proleptic Gregorian date, month 1..12.
// Howard Hinnant's days_from_civil.
static int64_t utc_time_daysFromCivil(int64_t year, int64_t month,
                                      int64_t day) {
  year -= month <= 2;
  int64_t era = (year >= 0 ? year : year - 399) / 400;
  int64_t yoe = year - era * 400;
  int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * DAYS_PER_ERA + doe - DAYS_TO_1970;
}

static int64_t utc_time_floorDiv(int64_t a, int64_t b) {
  return a / b - ((a % b != 0) && ((a < 0) != (b < 0)));
}

struct tm *utc_time_toTm(time_t t, struct tm *tm) {
  int64_t secs = (int64_t)t;
  int64_t days = utc_time_floorDiv(secs, SECONDS_PER_DAY);
  int64_t rem = secs - days * SECONDS_PER_DAY;
  tm->tm_hour = (int)(rem / 3600);
  tm->tm_min = (int)(rem % 3600 / 60);
  tm->tm_sec = (int)(rem % 60);
  // 1970-01-01 was a Thursday.
  tm->tm_wday = (int)(((days % 7) + 11) % 7);
  // Howard Hinnant's civil_from_days.
  int64_t z = days + DAYS_TO_1970;
  int64_t era = (z >= 0 ? z : z - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
  int64_t doe = z - era * DAYS_PER_ERA;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t day = doy - (153 * mp + 2) / 5 + 1;
  int64_t month = mp < 10 ? mp + 3 : mp - 9;
  int64_t year = yoe + era * 400 + (month <= 2);
  tm->tm_mday = (int)day;
  tm->tm_mon = (int)(month - 1);
  tm->tm_year = (int)(year - TM_YEAR_BASE);
  tm->tm_yday = (int)(days - utc_time_daysFromCivil(year, 1, 1));
  tm->tm_isdst = 0;
  return tm;
}

time_t utc_time_fromTm(struct tm *tm) {
  // Carry the month into the year first, so month 12 or -1 is allowed.
  int64_t months = (int64_t)tm->tm_year * MONTHS_PER_YEAR + tm->tm_mon;
  int64_t year = utc_time_floorDiv(months, MONTHS_PER_YEAR) + TM_YEAR_BASE;
  int64_t month =
      months - utc_time_floorDiv(months, MONTHS_PER_YEAR) * MONTHS_PER_YEAR;
  int64_t days = utc_time_daysFromCivil(year, month + 1, 1) + tm->tm_mday - 1;
  time_t t = (time_t)(days * SECONDS_PER_DAY + (int64_t)tm->tm_hour * 3600 +
                      (int64_t)tm->tm_min * 60 + tm->tm_sec);
  utc_time_toTm(t, tm);
  return t;
}
// NOLINTEND(readability-magic-numbers)

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
// newlib's own names: defined here, they are taken instead of newlib's
// versions, which would link the time-zone parser.
struct tm *localtime_r(const time_t *time, struct tm *tm) {
  return utc_time_toTm(*time, tm);
}

struct tm *gmtime_r(const time_t *time, struct tm *tm) {
  return utc_time_toTm(*time, tm);
}

time_t mktime(struct tm *tm) { return utc_time_fromTm(tm); }
#endif
