/**
 * File: uuid.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: UUID text checks shared by the app manager and the lookup table
 */

#include "uuid.h"

#include <ctype.h>
#include <stddef.h>

// Helper function to check if a 36-character string is a valid UUID4.
// A valid UUID4 is in the canonical format
// "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx" where x is a hexadecimal digit and y
// is one of [8, 9, A, B] (case-insensitive).
int uuid_is_valid4(const char *uuid) {
  // Check length: assume uuid is exactly 36 characters (without a null
  // terminator). Check that the hyphens are in the correct positions.
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (uuid[i] != '-') {
        return 0;
      }
    } else {
      if (!isxdigit((unsigned char)uuid[i])) {
        return 0;
      }
    }
  }
  // Check the version: the first character of the third group (index 14) must
  // be '4'
  if (uuid[14] != '4') {
    return 0;
  }
  // Check the variant: the first character of the fourth group (index 19)
  // must be one of '8', '9', 'A', or 'B' (case-insensitive)
  char variant = uuid[19];
  if (!(variant == '8' || variant == '9' || variant == 'A' || variant == 'B' ||
        variant == 'a' || variant == 'b')) {
    return 0;
  }
  return 1;
}

int uuid_is_exact4(const char *uuid) {
  return uuid != NULL && uuid_is_valid4(uuid) && uuid[UUID_TEXT_LENGTH] == '\0';
}
