// units: booster/src/lookuptable.c booster/src/uuid.c
/* The config region holds 30 sectors, 0 to 29. With all 30 taken, the
 * allocator says there is no free sector instead of handing out sector 30,
 * the flash sector of the lookup table itself (31 is the global settings).
 * Deleting an app frees its sector for the next install. */
#include "lookuptable_fixture.h"
#include "test.h"

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;
  for (unsigned n = 1; n <= CONFIG_SECTORS; n++) {
    int16_t sector =
        lookuptable_find_first_empty_sector(table, &length, CONFIG_SECTORS);
    CHECK_EQ(sector, n - 1);
    CHECK_EQ(lookuptable_update(uuidFor(n), (uint16_t)sector, table, &length),
             0);
  }
  CHECK_EQ(lookuptable_find_first_empty_sector(table, &length, CONFIG_SECTORS),
           -1);

  // Delete the app in sector 11; the next install gets sector 11.
  CHECK_EQ(lookuptable_delete(uuidFor(12), table, &length), 0);
  CHECK_EQ(lookuptable_find_first_empty_sector(table, &length, CONFIG_SECTORS),
           11);
  free(table);
  TEST_END();
}
