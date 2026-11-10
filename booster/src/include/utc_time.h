/**
 * File: utc_time.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Calendar time in UTC, without newlib's time-zone code
 */

#ifndef UTC_TIME_H
#define UTC_TIME_H

#include <time.h>

/**
 * @brief Seconds since 1970-01-01 00:00:00 UTC for a broken-down UTC time.
 *
 * Like timegm(): fields out of their normal range are carried (month 12 is
 * January of the next year, second 60 the next minute), and tm is then
 * rewritten normalised, with tm_wday and tm_yday filled in and tm_isdst 0.
 */
time_t utc_time_fromTm(struct tm *tm);

/**
 * @brief Broken-down UTC time for t; returns tm. Like gmtime_r().
 */
struct tm *utc_time_toTm(time_t t, struct tm *tm);

#endif  // UTC_TIME_H
