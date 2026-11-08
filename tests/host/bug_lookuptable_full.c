// units: booster/src/lookuptable.c booster/src/uuid.c
/* A 4K table holds 107 entries of 38 bytes. Adding a 108th must be refused;
 * lookuptable_update() appends it past the end of the 4K buffer instead. Its
 * "table full" branch can never run: the free slot starts out as the index
 * after the last entry rather than "none". On the device the table buffer is
 * on the stack, and the app manager then programs the 4K sector after the
 * table's, the global settings. */
#include "lookuptable_fixture.h"
#include "test.h"

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;
  const unsigned capacity = TABLE_SECTOR_SIZE / LOOKUP_ENTRY_SIZE;
  CHECK_EQ(capacity, 107);
  for (unsigned n = 1; n <= capacity; n++) {
    CHECK_EQ(lookuptable_update(uuidFor(n), (uint16_t)n, table, &length), 0);
  }
  CHECK(lookuptable_update(uuidFor(capacity + 1), 0, table, &length) != 0);
  CHECK(length <= TABLE_SECTOR_SIZE);
  free(table);
  TEST_END();
}
