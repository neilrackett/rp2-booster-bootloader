/**
 * File: uuid.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: UUID text checks shared by the app manager and the lookup table
 */

#ifndef UUID_H
#define UUID_H

// Length of a UUID in its canonical text form, without the terminator.
#define UUID_TEXT_LENGTH 36

/**
 * @brief Is uuid a version 4 UUID in canonical text form,
 * "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx", with y one of 8, 9, a, b?
 *
 * Reads at most UUID_TEXT_LENGTH characters and stops at the first one that
 * does not fit, so a shorter terminated string is safe to pass. Characters
 * after the 36th are not looked at.
 *
 * @return 1 if it is, 0 if not.
 */
int uuid_is_valid4(const char *uuid);

/**
 * @brief Is the whole string a version 4 UUID, nothing before or after it?
 *
 * Use this for any UUID that goes into a file path: uuid_is_valid4() accepts
 * "<uuid>/../../x", whose first 36 characters are a UUID.
 *
 * @return 1 if it is, 0 if not, or if uuid is NULL.
 */
int uuid_is_exact4(const char *uuid);

#endif  // UUID_H
