/**
 * File: vercmp.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Version string comparison for the firmware update check
 */

#ifndef VERCMP_H
#define VERCMP_H

#include <stdbool.h>

/**
 * @brief Does the string hold a version and nothing else?
 *
 * An optional "v", three numbers separated by dots, an optional suffix of
 * letters, digits, '.', '+' or '-', and whitespace around it: "v2.5.0",
 * "2.5.0\n" and "v2.5.0beta" pass; an error page or "v2.5" does not.
 */
bool vercmp_isVersion(const char *str);

/**
 * @brief Is candidate a newer version than current?
 *
 * candidate must pass vercmp_isVersion(): anything else is never newer.
 * Compares the first three numbers found in each string, so "v2.5.0",
 * "2.5.0" and "2.5.0beta" are all 2.5.0. For current, a missing number counts
 * as 0, and a NULL string as 0.0.0. Any suffix after the numbers is ignored.
 */
bool vercmp_isNewer(const char *candidate, const char *current);

#endif  // VERCMP_H
