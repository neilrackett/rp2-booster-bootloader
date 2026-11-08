/**
 * File: lookuptable.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The apps lookup table in RAM: which config sector belongs to
 * which app
 */

#ifndef LOOKUPTABLE_H
#define LOOKUPTABLE_H

#include <stdint.h>

// The table maps each installed app to the config sector that holds its
// settings. It is a run of entries from the start of a 4K flash sector, read
// into RAM, changed there and written back by the app manager. Each entry is
// 38 bytes:
//   - 36 bytes for the UUID, as text, no terminator
//   - 2 bytes for the sector (page number), little-endian
// An entry whose first byte is 0, or that is not a valid UUID, ends the table.
#define LOOKUP_ENTRY_SIZE 38

// The table is one 4K flash sector: 107 entries.
#define LOOKUP_TABLE_SIZE 4096
#define LOOKUP_TABLE_MAX_ENTRIES (LOOKUP_TABLE_SIZE / LOOKUP_ENTRY_SIZE)

// lookuptable_update(): the table has no room for another entry.
#define LOOKUPTABLE_FULL (-4)

/**
 * @brief The sector stored for uuid, or -1 if the table has no entry for it.
 * Stops at the first empty or invalid entry.
 */
int16_t lookuptable_get_page_for_uuid(const char *uuid, uint8_t *table,
                                      uint16_t length);

/**
 * @brief The sector stored for uuid.
 * @return The sector, or -1 if uuid is not 36 characters, -2 if it is not a
 * version 4 UUID, -3 if the table has no entry for it.
 */
int lookuptable_get_sector(const char *uuid, uint8_t *table,
                           uint16_t *table_length);

/**
 * @brief Sets the sector of uuid's entry, appending an entry if there is
 * none. *table_length grows by LOOKUP_ENTRY_SIZE on an append.
 * @return 0 on success, -1 if uuid is not 36 characters, -2 if it is not a
 * version 4 UUID, LOOKUPTABLE_FULL if a new entry would not fit in
 * LOOKUP_TABLE_SIZE.
 */
int lookuptable_update(const char *uuid, uint16_t sector, uint8_t *table,
                       uint16_t *table_length);

/**
 * @brief Removes uuid's entry and moves the later entries up, so the table
 * stays contiguous. *table_length shrinks by LOOKUP_ENTRY_SIZE.
 * @return 0 on success, -1 if uuid is not 36 characters or has no entry, -2
 * if it is not a version 4 UUID.
 */
int lookuptable_delete(const char *uuid, uint8_t *table,
                       uint16_t *table_length);

/**
 * @brief The lowest config sector, of the num_sectors that exist, that no
 * entry uses; -1 when all of them are taken.
 */
int16_t lookuptable_find_first_empty_sector(uint8_t *table, uint16_t *length,
                                            uint16_t num_sectors);

/**
 * @brief Drops every entry whose sector does not exist (num_sectors or above)
 * or is already used by an earlier entry, and moves the rest up so the table
 * stays contiguous. A device that handed out sectors past the last one, as
 * firmware before v2.5.0 did with more than 30 apps, recovers through this.
 *
 * @return How many entries were dropped; *table_length shrinks to match.
 */
int lookuptable_sanitize(uint8_t *table, uint16_t *table_length,
                         uint16_t num_sectors);

/**
 * @brief Moves uuid's entry to the end of the table, keeping the others in
 * order. The table is kept in launch order: the first entry is the least
 * recently launched app, the last the most recent. Microfirmwares find their
 * entry by UUID, so the order means nothing to them.
 *
 * @return 1 if the entry moved, 0 if it was already the last, -3 if the table
 * has no entry for uuid.
 */
int lookuptable_move_to_end(const char *uuid, uint8_t *table,
                            uint16_t table_length);

#endif  // LOOKUPTABLE_H
