// units: booster/src/lookuptable.c booster/src/uuid.c
/* The config region holds 30 sectors, 0 to 29. With all 30 taken, the
 * allocator must say there is no free sector; it returns 30. Sector 30 is
 * the flash sector of the lookup table itself and 31 the global settings
 * (WiFi, boot app): the 31st installed app is given the table's sector, and
 * the first time it saves its settings it overwrites the table; the 32nd
 * overwrites the global settings. The allocator counts up to the number of
 * table entries, 107, instead of the number of config sectors. */
#include "lookuptable_fixture.h"
#include "test.h"

int main(void) {
  uint8_t *table = newTable();
  uint16_t length = 0;
  for (unsigned n = 1; n <= CONFIG_SECTORS; n++) {
    int16_t sector = lookuptable_find_first_empty_sector(table, &length);
    lookuptable_update(uuidFor(n), (uint16_t)sector, table, &length);
  }
  int16_t next = lookuptable_find_first_empty_sector(table, &length);
  CHECK(next < CONFIG_SECTORS);
  free(table);
  TEST_END();
}
