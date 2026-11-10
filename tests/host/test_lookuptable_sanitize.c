// units: booster/src/lookuptable.c booster/src/uuid.c
/* A device that ran firmware before v2.5.0 with more than 30 apps has lookup
 * entries for sectors 30 and 31, and an install that failed after its sync
 * fallback left two apps on sector 0. lookuptable_sanitize() drops those
 * entries, keeps the first app on a shared sector, and keeps the table
 * contiguous; the apps it dropped get a sector of their own at the next
 * assignment. */
#include "lookuptable_fixture.h"
#include "test.h"

static void put(uint8_t *table, uint16_t *length, unsigned n, uint16_t sector) {
  CHECK_EQ(lookuptable_update(uuidFor(n), sector, table, length), 0);
}

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;
  put(table, &length, 1, 0);
  put(table, &length, 2, 30);  // the lookup table's own sector
  put(table, &length, 3, 1);
  put(table, &length, 4, 0);   // shares sector 0 with app 1
  put(table, &length, 5, 31);  // the global settings
  put(table, &length, 6, 29);

  CHECK_EQ(lookuptable_sanitize(table, &length, CONFIG_SECTORS), 3);
  CHECK_EQ(length, 3 * LOOKUP_ENTRY_SIZE);
  CHECK_EQ(lookuptable_get_sector(uuidFor(1), table, &length), 0);
  CHECK_EQ(lookuptable_get_sector(uuidFor(3), table, &length), 1);
  CHECK_EQ(lookuptable_get_sector(uuidFor(6), table, &length), 29);
  CHECK_EQ(lookuptable_get_sector(uuidFor(2), table, &length), -3);
  CHECK_EQ(lookuptable_get_sector(uuidFor(4), table, &length), -3);
  CHECK_EQ(lookuptable_get_sector(uuidFor(5), table, &length), -3);
  // What follows the kept entries is cleared.
  for (size_t i = length; i < 6 * LOOKUP_ENTRY_SIZE; i++) {
    CHECK_EQ(table[i], 0);
  }
  // A clean table is left alone.
  CHECK_EQ(lookuptable_sanitize(table, &length, CONFIG_SECTORS), 0);
  CHECK_EQ(length, 3 * LOOKUP_ENTRY_SIZE);
  // The dropped apps can be given free sectors again.
  CHECK_EQ(lookuptable_find_first_empty_sector(table, &length, CONFIG_SECTORS),
           2);
  free(table);
  TEST_END();
}
