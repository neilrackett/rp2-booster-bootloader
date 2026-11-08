// units: booster/src/utc_time.c
/* utc_time_toTm() and utc_time_fromTm() against the host's gmtime_r() and
 * timegm(): every day from 1900 to 2200 at a few times of day, the leap-year
 * edges, FatFs's range (1980 to 2107), and the out-of-range fields mktime()
 * must carry. The device build uses them as localtime_r() and mktime(). */
#define _DEFAULT_SOURCE
#include <string.h>
#include <time.h>

#include "test.h"
#include "utc_time.h"

static int sameTm(const struct tm *a, const struct tm *b) {
  return a->tm_year == b->tm_year && a->tm_mon == b->tm_mon &&
         a->tm_mday == b->tm_mday && a->tm_hour == b->tm_hour &&
         a->tm_min == b->tm_min && a->tm_sec == b->tm_sec &&
         a->tm_wday == b->tm_wday && a->tm_yday == b->tm_yday;
}

int main(void) {
  int mismatches = 0;
  // Every day from 1900-01-01 to 2200-01-01, at three times of day.
  const time_t first = -2208988800;  // 1900-01-01 00:00:00
  const time_t last = 7258118400;    // 2200-01-01 00:00:00
  for (time_t day = first; day <= last; day += 86400) {
    const time_t offsets[] = {0, 43199, 86399};
    for (int k = 0; k < 3; k++) {
      time_t t = day + offsets[k];
      struct tm ours, host;
      memset(&ours, 0, sizeof(ours));
      utc_time_toTm(t, &ours);
      gmtime_r(&t, &host);
      if (!sameTm(&ours, &host) || ours.tm_isdst != 0) mismatches++;
      struct tm back = host;
      if (utc_time_fromTm(&back) != t || !sameTm(&back, &host)) mismatches++;
    }
  }
  CHECK_EQ(mismatches, 0);

  // Known instants.
  struct tm tm;
  utc_time_toTm(0, &tm);
  CHECK(tm.tm_year == 70 && tm.tm_mon == 0 && tm.tm_mday == 1 &&
        tm.tm_wday == 4 && tm.tm_yday == 0);
  utc_time_toTm(951782400, &tm);  // 2000-02-29, a leap day
  CHECK(tm.tm_year == 100 && tm.tm_mon == 1 && tm.tm_mday == 29);
  utc_time_toTm(315532800, &tm);  // 1980-01-01, the start of FatFs time
  CHECK(tm.tm_year == 80 && tm.tm_mon == 0 && tm.tm_mday == 1 &&
        tm.tm_wday == 2);

  // Out-of-range fields are carried, as mktime() and timegm() do.
  struct tm odd = {.tm_year = 126, .tm_mon = 12, .tm_mday = 1};  // 2026-13-01
  struct tm hostOdd = odd;
  CHECK_EQ(utc_time_fromTm(&odd), timegm(&hostOdd));
  CHECK(odd.tm_year == 127 && odd.tm_mon == 0 && odd.tm_mday == 1);
  struct tm under = {.tm_year = 126,
                     .tm_mon = -1,
                     .tm_mday = 31,
                     .tm_hour = 25,
                     .tm_min = -5,
                     .tm_sec = 61};
  struct tm hostUnder = under;
  CHECK_EQ(utc_time_fromTm(&under), timegm(&hostUnder));
  CHECK(sameTm(&under, &hostUnder));
  struct tm feb30 = {.tm_year = 123, .tm_mon = 1, .tm_mday = 30};
  struct tm hostFeb30 = feb30;
  CHECK_EQ(utc_time_fromTm(&feb30), timegm(&hostFeb30));
  CHECK(feb30.tm_mon == 2 && feb30.tm_mday == 2);
  TEST_END();
}
