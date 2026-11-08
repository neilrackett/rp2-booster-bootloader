// units: booster/src/lookuptable.c booster/src/uuid.c
/* A 4K table holds 107 entries of 38 bytes. A 108th is refused with
 * LOOKUPTABLE_FULL and nothing is written past the 4K buffer (the address
 * sanitizer would abort); an existing entry can still be updated. */
#include "lookuptable_fixture.h"
#include "test.h"

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;
  const unsigned capacity = TABLE_SECTOR_SIZE / LOOKUP_ENTRY_SIZE;
  CHECK_EQ(capacity, 107);
  CHECK_EQ(LOOKUP_TABLE_MAX_ENTRIES, 107);
  for (unsigned n = 1; n <= capacity; n++) {
    CHECK_EQ(lookuptable_update(uuidFor(n), (uint16_t)n, table, &length), 0);
  }
  CHECK_EQ(lookuptable_update(uuidFor(capacity + 1), 0, table, &length),
           LOOKUPTABLE_FULL);
  CHECK_EQ(length, capacity * LOOKUP_ENTRY_SIZE);
  CHECK(length <= TABLE_SECTOR_SIZE);
  CHECK_EQ(lookuptable_update(uuidFor(5), 99, table, &length), 0);
  CHECK_EQ(lookuptable_get_sector(uuidFor(5), table, &length), 99);
  free(table);
  TEST_END();
}
