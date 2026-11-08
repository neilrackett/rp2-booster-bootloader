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
 * version 4 UUID.
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
 * @brief The lowest sector number no entry uses.
 */
int16_t lookuptable_find_first_empty_sector(uint8_t *table, uint16_t *length);

#endif  // LOOKUPTABLE_H
