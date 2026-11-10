/* Helpers for the lookup table tests: a 4K table on the heap, so the address
 * sanitizer sees a write past its end, and distinct version 4 UUIDs. */
#ifndef HOST_LOOKUPTABLE_FIXTURE_H
#define HOST_LOOKUPTABLE_FIXTURE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lookuptable.h"

#define TABLE_SECTOR_SIZE 4096
// The config region holds this many 4K sectors, one per app.
#define CONFIG_SECTORS 30

static uint8_t *newTable(void) { return calloc(1, TABLE_SECTOR_SIZE); }

// The n-th test UUID: "00000000-0000-4000-8000-<n as 12 hex digits>".
static const char *uuidFor(unsigned n) {
  static char text[8][37];
  static unsigned next;
  char *out = text[next++ % 8];
  snprintf(out, 37, "00000000-0000-4000-8000-%012x", n);
  return out;
}

#endif
