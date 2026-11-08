// units: booster/src/lookuptable.c booster/src/uuid.c
/* The apps lookup table as the app manager uses it: install appends an
 * entry with the first free sector, launch finds an app's sector, delete
 * removes the entry and frees the sector for the next install. */
#include "lookuptable_fixture.h"
#include "test.h"

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;

  CHECK_EQ(lookuptable_find_first_empty_sector(table, &length), 0);
  CHECK_EQ(lookuptable_get_sector(uuidFor(1), table, &length), -3);

  // Three installs take sectors 0, 1 and 2.
  for (unsigned n = 1; n <= 3; n++) {
    int16_t sector = lookuptable_find_first_empty_sector(table, &length);
    CHECK_EQ(sector, n - 1);
    CHECK_EQ(lookuptable_update(uuidFor(n), (uint16_t)sector, table, &length),
             0);
  }
  CHECK_EQ(length, 3 * LOOKUP_ENTRY_SIZE);
  CHECK_EQ(lookuptable_get_sector(uuidFor(2), table, &length), 1);
  CHECK_EQ(lookuptable_get_page_for_uuid(uuidFor(3), table, length), 2);
  CHECK_EQ(lookuptable_get_page_for_uuid(uuidFor(9), table, length), -1);

  // The entry's bytes: the UUID as text, then the sector little-endian.
  CHECK(memcmp(table + LOOKUP_ENTRY_SIZE, uuidFor(2), 36) == 0);
  CHECK_EQ(table[LOOKUP_ENTRY_SIZE + 36], 1);
  CHECK_EQ(table[LOOKUP_ENTRY_SIZE + 37], 0);

  // Updating an existing entry changes its sector and does not append.
  CHECK_EQ(lookuptable_update(uuidFor(2), 7, table, &length), 0);
  CHECK_EQ(length, 3 * LOOKUP_ENTRY_SIZE);
  CHECK_EQ(lookuptable_get_sector(uuidFor(2), table, &length), 7);
  CHECK_EQ(lookuptable_update(uuidFor(2), 1, table, &length), 0);

  // Deleting the middle entry moves the last one up and frees its sector.
  CHECK_EQ(lookuptable_delete(uuidFor(2), table, &length), 0);
  CHECK_EQ(length, 2 * LOOKUP_ENTRY_SIZE);
  CHECK(memcmp(table + LOOKUP_ENTRY_SIZE, uuidFor(3), 36) == 0);
  CHECK_EQ(table[2 * LOOKUP_ENTRY_SIZE], 0);
  CHECK_EQ(lookuptable_get_sector(uuidFor(2), table, &length), -3);
  CHECK_EQ(lookuptable_get_sector(uuidFor(3), table, &length), 2);
  CHECK_EQ(lookuptable_find_first_empty_sector(table, &length), 1);
  CHECK_EQ(lookuptable_delete(uuidFor(2), table, &length), -1);

  // Deleting the last entry and the only entry.
  CHECK_EQ(lookuptable_delete(uuidFor(3), table, &length), 0);
  CHECK_EQ(lookuptable_delete(uuidFor(1), table, &length), 0);
  CHECK_EQ(length, 0);
  CHECK_EQ(lookuptable_find_first_empty_sector(table, &length), 0);

  // A UUID of the wrong length or form is refused before the table is read.
  CHECK_EQ(lookuptable_get_sector("short", table, &length), -1);
  CHECK_EQ(lookuptable_update("00000000-0000-3000-8000-000000000001", 0, table,
                              &length),
           -2);
  CHECK_EQ(lookuptable_delete("../../../../../../../../../../../../", table,
                              &length),
           -2);
  CHECK_EQ(length, 0);

  // 30 installs fill sectors 0 to 29, one each.
  for (unsigned n = 1; n <= CONFIG_SECTORS; n++) {
    int16_t sector = lookuptable_find_first_empty_sector(table, &length);
    CHECK_EQ(sector, n - 1);
    lookuptable_update(uuidFor(n), (uint16_t)sector, table, &length);
  }
  CHECK_EQ(length, CONFIG_SECTORS * LOOKUP_ENTRY_SIZE);

  free(table);
  TEST_END();
}
