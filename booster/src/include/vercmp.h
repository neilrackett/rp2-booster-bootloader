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
 * @brief Is candidate a newer version than current?
 *
 * Compares the first three numbers found in each string, so "v2.5.0",
 * "2.5.0" and "2.5.0beta" are all 2.5.0. A missing number counts as 0, and
 * a NULL string as 0.0.0. Any suffix after the numbers is ignored.
 */
bool vercmp_isNewer(const char *candidate, const char *current);

#endif  // VERCMP_H
