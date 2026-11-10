// units: booster/src/lookuptable.c booster/src/uuid.c
/* When all 30 config sectors are taken, the least recently launched app that
 * is not pinned gives up its sector (its settings go to the card). The table
 * is in launch order, so that is the first unpinned entry. */
#include "lookuptable_fixture.h"
#include "test.h"

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;
  CHECK_EQ(lookuptable_least_recent(table, length, NULL, 0), -1);

  for (unsigned n = 1; n <= CONFIG_SECTORS; n++) {
    lookuptable_update(uuidFor(n), (uint16_t)(n - 1), table, &length);
  }
  CHECK_EQ(lookuptable_least_recent(table, length, NULL, 0), 0);

  // App 1 is launched again: it is now the most recent, app 2 the least.
  CHECK_EQ(lookuptable_move_to_end(uuidFor(1), table, length), 1);
  CHECK_EQ(lookuptable_least_recent(table, length, NULL, 0), 0);
  CHECK(memcmp(table, uuidFor(2), 36) == 0);

  // Pinned apps are skipped, wherever they are; NULL pins are ignored.
  char pin2[37], pin3[37];
  strcpy(pin2, uuidFor(2));
  strcpy(pin3, uuidFor(3));
  const char *pins[] = {NULL, pin2, pin3};
  int victim = lookuptable_least_recent(table, length, pins, 3);
  CHECK_EQ(victim, 2);
  CHECK(memcmp(table + victim * LOOKUP_ENTRY_SIZE, uuidFor(4), 36) == 0);

  // Everything pinned: no victim.
  uint8_t *two = newTable();
  uint16_t twoLength = 0;
  lookuptable_update(pin2, 0, two, &twoLength);
  lookuptable_update(pin3, 1, two, &twoLength);
  CHECK_EQ(lookuptable_least_recent(two, twoLength, pins, 3), -1);
  free(two);
  free(table);
  TEST_END();
}
