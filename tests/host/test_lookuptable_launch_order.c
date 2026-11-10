// units: booster/src/lookuptable.c booster/src/uuid.c
/* The lookup table is kept in launch order: launching an app moves its entry
 * to the end, so the first entry is the least recently launched. An app
 * launched for the first time is appended, which is also the end. Sectors
 * never change with the order. */
#include "lookuptable_fixture.h"
#include "test.h"

static void launch(uint8_t *table, uint16_t *length, unsigned n) {
  int moved = lookuptable_move_to_end(uuidFor(n), table, *length);
  if (moved < 0) {
    int16_t sector =
        lookuptable_find_first_empty_sector(table, length, CONFIG_SECTORS);
    CHECK(sector >= 0);
    CHECK_EQ(lookuptable_update(uuidFor(n), (uint16_t)sector, table, length),
             0);
  }
}

static unsigned entryAt(const uint8_t *table, unsigned index) {
  for (unsigned n = 1; n <= 10; n++) {
    if (memcmp(table + index * LOOKUP_ENTRY_SIZE, uuidFor(n), 36) == 0) {
      return n;
    }
  }
  return 0;
}

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;
  CHECK_EQ(lookuptable_move_to_end(uuidFor(1), table, length), -3);

  launch(table, &length, 1);  // sector 0
  launch(table, &length, 2);  // sector 1
  launch(table, &length, 3);  // sector 2
  CHECK_EQ(entryAt(table, 0), 1);
  CHECK_EQ(entryAt(table, 2), 3);

  // Launching the last one again changes nothing.
  CHECK_EQ(lookuptable_move_to_end(uuidFor(3), table, length), 0);

  // Launching the first moves it to the end; its sector stays.
  CHECK_EQ(lookuptable_move_to_end(uuidFor(1), table, length), 1);
  CHECK_EQ(entryAt(table, 0), 2);
  CHECK_EQ(entryAt(table, 1), 3);
  CHECK_EQ(entryAt(table, 2), 1);
  CHECK_EQ(lookuptable_get_sector(uuidFor(1), table, &length), 0);
  CHECK_EQ(lookuptable_get_sector(uuidFor(2), table, &length), 1);
  CHECK_EQ(length, 3 * LOOKUP_ENTRY_SIZE);

  // A middle one, then a new app: order 2, 1, 3, 4.
  launch(table, &length, 3);
  CHECK_EQ(entryAt(table, 0), 2);
  CHECK_EQ(entryAt(table, 1), 1);
  CHECK_EQ(entryAt(table, 2), 3);
  launch(table, &length, 4);
  CHECK_EQ(entryAt(table, 3), 4);
  CHECK_EQ(lookuptable_get_sector(uuidFor(4), table, &length), 3);

  // Deleting keeps the order of the rest.
  CHECK_EQ(lookuptable_delete(uuidFor(1), table, &length), 0);
  CHECK_EQ(entryAt(table, 0), 2);
  CHECK_EQ(entryAt(table, 1), 3);
  CHECK_EQ(entryAt(table, 2), 4);
  free(table);
  TEST_END();
}
