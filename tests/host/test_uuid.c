// units: booster/src/uuid.c
/* uuid_is_valid4(): the catalog's UUIDs, the development app's fixed one,
 * and the ways a UUID from a file, a URL or the lookup table can be wrong. */
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "uuid.h"

int main(void) {
  CHECK_EQ(uuid_is_valid4("44444444-4444-4444-8444-444444444444"), 1);
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-4ea1-9264-6fe13da448a0"), 1);
  CHECK_EQ(uuid_is_valid4("1805C20C-5B83-4EA1-B264-6FE13DA448A0"), 1);
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-4ea1-a264-6fe13da448a0"), 1);

  // Version digit is not 4; variant is not 8, 9, a or b.
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-3ea1-9264-6fe13da448a0"), 0);
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-4ea1-c264-6fe13da448a0"), 0);
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-4ea1-7264-6fe13da448a0"), 0);
  // Hyphens missing or moved, a non-hex digit.
  CHECK_EQ(uuid_is_valid4("1805c20c05b83-4ea1-9264-6fe13da448a0"), 0);
  CHECK_EQ(uuid_is_valid4("1805c20-c5b83-4ea1-9264-6fe13da448a0"), 0);
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-4ea1-9264-6fe13da448ag"), 0);
  // Path characters, as a uuid taken from a request would carry them.
  CHECK_EQ(uuid_is_valid4("../../../../../../../../../../../../.."), 0);
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-4ea1-9264-6fe13da4/../a"), 0);
  CHECK_EQ(uuid_is_valid4(""), 0);

  // A short string is rejected at its terminator: copied to the heap so the
  // address sanitizer catches any read past it.
  const char *shortText = "1805c20c-5b83-4ea1";
  char *heap = malloc(strlen(shortText) + 1);
  strcpy(heap, shortText);
  CHECK_EQ(uuid_is_valid4(heap), 0);
  free(heap);

  // Only the first 36 characters are looked at.
  CHECK_EQ(uuid_is_valid4("1805c20c-5b83-4ea1-9264-6fe13da448a0.json"), 1);
  TEST_END();
}
