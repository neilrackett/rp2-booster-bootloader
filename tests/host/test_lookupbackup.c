// units: booster/src/lookupbackup.c
/* The lookup table's backup on the card: a round trip, the CRC-32 check
 * value, and every reason a backup must be refused (another device's card,
 * a cut-short or altered file, a length that is not whole entries). */
#include <stdlib.h>
#include <string.h>

#include "lookupbackup.h"
#include "lookuptable.h"
#include "test.h"

int main(void) {
  // The standard CRC-32 check value.
  CHECK_EQ(lookupbackup_crc32((const uint8_t *)"123456789", 9), 0xCBF43926);

  const uint8_t me[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint8_t other[8] = {1, 2, 3, 4, 5, 6, 7, 9};
  uint8_t table[3 * LOOKUP_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(table); i++) table[i] = (uint8_t)(i * 7);

  uint8_t *file = malloc(LOOKUPBACKUP_HEADER_SIZE + sizeof(table));
  size_t n = lookupbackup_encode(file, table, sizeof(table), me);
  CHECK_EQ(n, LOOKUPBACKUP_HEADER_SIZE + sizeof(table));
  CHECK_EQ(lookupbackup_validate(file, n, me, LOOKUP_TABLE_SIZE),
           (int)sizeof(table));
  CHECK(memcmp(file + LOOKUPBACKUP_HEADER_SIZE, table, sizeof(table)) == 0);

  // Another device's card, a cut-short file, a changed byte, a bad magic.
  CHECK_EQ(lookupbackup_validate(file, n, other, LOOKUP_TABLE_SIZE), -1);
  CHECK_EQ(lookupbackup_validate(file, n - 1, me, LOOKUP_TABLE_SIZE), -1);
  file[LOOKUPBACKUP_HEADER_SIZE + 5] ^= 1;
  CHECK_EQ(lookupbackup_validate(file, n, me, LOOKUP_TABLE_SIZE), -1);
  file[LOOKUPBACKUP_HEADER_SIZE + 5] ^= 1;
  file[0] = 'X';
  CHECK_EQ(lookupbackup_validate(file, n, me, LOOKUP_TABLE_SIZE), -1);
  file[0] = 'B';
  CHECK_EQ(lookupbackup_validate(file, n, me, LOOKUP_TABLE_SIZE),
           (int)sizeof(table));
  CHECK_EQ(lookupbackup_validate(file, 10, me, LOOKUP_TABLE_SIZE), -1);
  // A table larger than allowed.
  CHECK_EQ(lookupbackup_validate(file, n, me, 2 * LOOKUP_ENTRY_SIZE), -1);
  free(file);

  // A length that is not whole entries.
  uint8_t odd[LOOKUPBACKUP_HEADER_SIZE + 37];
  lookupbackup_encode(odd, table, 37, me);
  CHECK_EQ(lookupbackup_validate(odd, sizeof(odd), me, LOOKUP_TABLE_SIZE), -1);

  // An empty table is a valid backup: every app deleted.
  uint8_t empty[LOOKUPBACKUP_HEADER_SIZE];
  CHECK_EQ(lookupbackup_encode(empty, table, 0, me), LOOKUPBACKUP_HEADER_SIZE);
  CHECK_EQ(lookupbackup_validate(empty, sizeof(empty), me, LOOKUP_TABLE_SIZE),
           0);
  TEST_END();
}
