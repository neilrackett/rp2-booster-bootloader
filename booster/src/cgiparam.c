/**
 * File: cgiparam.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Query parameters of a CGI request
 */

#include "cgiparam.h"

#include <stddef.h>
#include <string.h>

const char *cgiparam_get(const char *name, int count, char *const names[],
                         char *const values[]) {
  if (name == NULL || names == NULL || values == NULL) {
    return NULL;
  }
  for (int i = 0; i < count; i++) {
    if (names[i] != NULL && strcmp(names[i], name) == 0) {
      return values[i];
    }
  }
  return NULL;
}
