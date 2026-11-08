/**
 * File: lookuptable.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The apps lookup table in RAM: which config sector belongs to
 * which app
 */

#include "lookuptable.h"

#include <stdbool.h>
#include <string.h>

#include "debug.h"
#include "uuid.h"

/**
 * Searches the lookup table for an entry matching the given UUID.
 *
 * @param uuid   A pointer to the 36-character UUID string to search for.
 * @param table  A pointer to the lookup table data in RAM.
 * @param length Total number of bytes stored in the lookup table.
 *
 * @return The page number (as a 16-bit integer) if found, or -1 if not found.
 */
int16_t lookuptable_get_page_for_uuid(const char *uuid, uint8_t *table,
                                      uint16_t length) {
  // Determine the number of entries in the table.
  uint16_t num_entries = length / LOOKUP_ENTRY_SIZE;

  for (uint16_t i = 0; i < num_entries; i++) {
    uint8_t *entry = table + (i * LOOKUP_ENTRY_SIZE);

    // If the first byte is zero, assume no further valid entries.
    if (entry[0] == 0) {
      break;
    }

    if (!uuid_is_valid4((const char *)entry)) {
      // Invalid UUID format: assume no further valid data.
      break;
    }

    // Compare the first 36 bytes with the given UUID.
    if (memcmp(entry, uuid, 36) == 0) {
      // Extract the page number from the two bytes following the UUID.
      // Assuming little-endian format.
      uint16_t page = entry[36] | (entry[37] << 8);
      return page;
    }
  }

  // UUID not found in the lookup table.
  return -1;
}

/**
 * @brief Retrieves the lookup table sector corresponding to a given UUID.
 *
 * @param uuid         The UUID string to search for. Must be exactly 36
 * characters in length.
 * @param table        Pointer to the lookup table containing the entries.
 * @param table_length Pointer to a variable holding the table's total length in
 * bytes. This could be a pointer to const if the length is not modified.
 *
 * @return The sector number if a matching UUID is found; otherwise, returns a
 * negative error code:
 *         - Returns -1 if the UUID length is invalid.
 *         - Returns -2 if the UUID format is invalid.
 *         - Returns -3 if the UUID was not found in the table.
 */
int lookuptable_get_sector(const char *uuid, uint8_t *table,
                           uint16_t *table_length) {
  int sector = -3;  // Default to -3 to indicate not found or error
  // Validate that the UUID is exactly 36 characters
  if (strlen(uuid) != 36) {
    DPRINTF("Invalid UUID length: %u. Expected 36 characters.\n",
            (unsigned)strlen(uuid));
    return -1;  // Error: invalid UUID length
  }

  if (!uuid_is_valid4(uuid)) {
    DPRINTF("Invalid UUID format: %s\n", uuid);
    return -2;  // Error: invalid UUID format
  }

  uint16_t num_entries = *table_length / LOOKUP_ENTRY_SIZE;
  int indexFound = -1;

  // Iterate through each entry in the table.
  for (uint16_t i = 0; i < num_entries; i++) {
    uint8_t *entry = table + (i * LOOKUP_ENTRY_SIZE);
    // If the entry is not empty (first byte is not 0), check if it matches the
    // UUID.
    if (entry[0] != 0) {
      if (memcmp(entry, uuid, 36) == 0) {
        indexFound = i;
        break;
      }
    }
  }

  // Return the sector if found.
  if (indexFound != -1) {
    uint8_t *entry = table + (indexFound * LOOKUP_ENTRY_SIZE);
    sector = entry[36] | (entry[37] << 8);
    DPRINTF("Return entry at index %d with sector %u.\n", indexFound, sector);
  }
  return sector;
}

int lookuptable_update(const char *uuid, uint16_t sector, uint8_t *table,
                       uint16_t *table_length) {
  // Validate that the UUID is exactly 36 characters
  if (strlen(uuid) != 36) {
    DPRINTF("Invalid UUID length: %u. Expected 36 characters.\n",
            (unsigned)strlen(uuid));
    return -1;
  }

  if (!uuid_is_valid4(uuid)) {
    DPRINTF("Invalid UUID format: %s\n", uuid);
    return -2;
  }

  uint16_t num_entries = *table_length / LOOKUP_ENTRY_SIZE;
  int indexFound = -1;
  // A new entry goes after the last one, while the table has room for it.
  int indexEmpty = (num_entries < LOOKUP_TABLE_MAX_ENTRIES) ? num_entries : -1;

  // Iterate through each entry in the table.
  for (uint16_t i = 0; i < num_entries; i++) {
    uint8_t *entry = table + (i * LOOKUP_ENTRY_SIZE);
    // If the entry is empty (first byte is 0), record the index if we haven't
    // found an empty slot yet.
    if (entry[0] == 0) {
      if (indexEmpty == -1) {
        indexEmpty = i;
      }
    } else {
      // Otherwise, compare the stored UUID (first 36 bytes) with the provided
      // UUID.
      if (memcmp(entry, uuid, 36) == 0) {
        indexFound = i;
        break;
      }
    }
  }

  if (indexFound != -1) {
    // Entry found: overwrite the sector value.
    uint8_t *entry = table + (indexFound * LOOKUP_ENTRY_SIZE);
    entry[36] = sector & 0xFF;
    entry[37] = (sector >> 8) & 0xFF;
    DPRINTF("Updated entry at index %d with sector %u.\n", indexFound, sector);
    return 0;
  } else if (indexEmpty != -1) {
    // No matching entry found, but there is an empty slot: append new entry.
    uint8_t *entry = table + (indexEmpty * LOOKUP_ENTRY_SIZE);
    memcpy(entry, uuid, 36);  // Copy the UUID exactly (36 bytes)
    entry[36] = sector & 0xFF;
    entry[37] = (sector >> 8) & 0xFF;
    *table_length += LOOKUP_ENTRY_SIZE;
    DPRINTF("Appended new entry at index %d: UUID = %.*s, sector = %u.\n",
            indexEmpty, 36, uuid, sector);
    return 0;
  } else {
    // No free slot available in the lookup table.
    DPRINTF("Lookup table is full; cannot append new entry.\n");
    return LOOKUPTABLE_FULL;
  }
}

/**
 * Delete an entry from the lookup table given its UUID.
 *
 * Each entry in the lookup table is 38 bytes:
 * - The first 36 bytes store the UUID (exactly as a 36-character string)
 * - The next 2 bytes store the sector (page number) in little-endian format.
 *
 * The table is stored as a contiguous block of memory. Valid entries are
 * assumed to be contiguous from the beginning of the table, and an entry is
 * considered empty if its first byte is zero.
 *
 * @param uuid         A pointer to the 36-character UUID string to delete.
 * @param table        Pointer to the lookup table data in memory.
 * @param table_length Total length (in bytes) of the lookup table.
 *
 * @return 0 on success, or -1 if the UUID is not found or if the UUID length is
 * invalid.
 */
int lookuptable_delete(const char *uuid, uint8_t *table,
                       uint16_t *table_length) {
  // Validate that the UUID is exactly 36 characters.
  if (strlen(uuid) != 36) {
    DPRINTF("Invalid UUID length: %u. Expected 36 characters.\n",
            (unsigned)strlen(uuid));
    return -1;
  }
  if (!uuid_is_valid4(uuid)) {
    DPRINTF("Invalid UUID format: %s\n", uuid);
    return -2;
  }

  // Calculate the total number of entries in the table.
  uint16_t num_entries = *table_length / LOOKUP_ENTRY_SIZE;
  int found_index = -1;

  // Determine how many valid (non-empty) entries are in the table.
  uint16_t valid_entries = 0;
  for (uint16_t i = 0; i < num_entries; i++) {
    uint8_t *entry = table + i * LOOKUP_ENTRY_SIZE;
    if (entry[0] == 0) {
      // Assume the table ends when an empty entry is encountered.
      break;
    }
    if (!uuid_is_valid4((const char *)entry)) {
      // Invalid UUID format: assume no further valid data.
      break;
    }
    valid_entries++;
  }

  // Search for the entry with the given UUID among the valid entries.
  for (uint16_t i = 0; i < valid_entries; i++) {
    uint8_t *entry = table + i * LOOKUP_ENTRY_SIZE;
    if (memcmp(entry, uuid, 36) == 0) {
      found_index = i;
      break;
    }
  }

  if (found_index == -1) {
    // UUID not found in the lookup table.
    DPRINTF("UUID not found in lookup table.\n");
    return -1;
  }

  // If the found entry is not the last one, shift subsequent entries upward to
  // compact the table.
  if (found_index < valid_entries - 1) {
    memmove(table + found_index * LOOKUP_ENTRY_SIZE,  // Destination: position
                                                      // of the deleted entry
            table + (found_index + 1) *
                        LOOKUP_ENTRY_SIZE,  // Source: next entry in the table
            (valid_entries - found_index - 1) *
                LOOKUP_ENTRY_SIZE  // Number of bytes to move
    );
  }

  // Clear the now-last valid entry.
  uint8_t *last_entry = table + ((valid_entries - 1) * LOOKUP_ENTRY_SIZE);
  memset(last_entry, 0, LOOKUP_ENTRY_SIZE);

  *table_length -= LOOKUP_ENTRY_SIZE;

  DPRINTF("Deleted entry for UUID: %s\n", uuid);
  return 0;
}

/**
 * Finds the first empty configuration sector from the lookup table provided.
 *
 * @param table       Pointer to the lookup table data (in memory).
 * @param length      Pointer to the total length (in bytes) of the lookup
 * table.
 * @param num_sectors How many config sectors exist (sectors 0 to
 * num_sectors-1).
 *
 * @return The lowest sector number no entry uses, or -1 if every one of the
 *         num_sectors sectors is taken.
 */
int16_t lookuptable_find_first_empty_sector(uint8_t *table, uint16_t *length,
                                            uint16_t num_sectors) {
  // Calculate the total number of entries in the provided table.
  uint16_t total_entries = *length / LOOKUP_ENTRY_SIZE;
  uint16_t valid_entries = 0;

  // Count valid entries until an empty entry is encountered.
  for (uint16_t i = 0; i < total_entries; i++) {
    uint8_t *entry = table + (i * LOOKUP_ENTRY_SIZE);
    if (entry[0] == 0) {  // An entry with first byte 0 is considered empty.
      break;
    }
    if (!uuid_is_valid4((const char *)entry)) {
      // Invalid UUID format: assume no further valid data.
      break;
    }
    valid_entries++;
  }

  // Only sectors that exist are candidates. Counting up to the number of table
  // entries instead handed the 31st app sector 30, the lookup table's own
  // flash sector, and the 32nd the global settings.
  for (uint16_t candidate = 0; candidate < num_sectors; candidate++) {
    bool used = false;
    for (uint16_t i = 0; i < valid_entries; i++) {
      uint8_t *entry = table + (i * LOOKUP_ENTRY_SIZE);
      // Extract the sector number stored in the entry (little-endian)
      uint16_t sector = entry[36] | (((uint16_t)entry[37]) << 8);
      if (sector == candidate) {
        used = true;
        break;
      }
    }
    if (!used) {
      return candidate;
    }
  }

  DPRINTF("All %u config sectors are in use.\n", num_sectors);
  return -1;
}

int lookuptable_sanitize(uint8_t *table, uint16_t *table_length,
                         uint16_t num_sectors) {
  uint16_t num_entries = *table_length / LOOKUP_ENTRY_SIZE;
  uint16_t kept = 0;
  int dropped = 0;
  for (uint16_t i = 0; i < num_entries; i++) {
    uint8_t *entry = table + (i * LOOKUP_ENTRY_SIZE);
    if (entry[0] == 0 || !uuid_is_valid4((const char *)entry)) {
      break;  // the table ends here
    }
    uint16_t sector = entry[36] | (((uint16_t)entry[37]) << 8);
    bool shared = false;
    for (uint16_t k = 0; k < kept; k++) {
      const uint8_t *other = table + (k * LOOKUP_ENTRY_SIZE);
      if ((other[36] | (((uint16_t)other[37]) << 8)) == sector) {
        shared = true;
        break;
      }
    }
    if (sector >= num_sectors || shared) {
      DPRINTF("Dropping lookup entry %.*s: sector %u %s\n", 36,
              (const char *)entry, sector,
              shared ? "already used by another app" : "does not exist");
      dropped++;
      continue;
    }
    if (kept != i) {
      memmove(table + (kept * LOOKUP_ENTRY_SIZE), entry, LOOKUP_ENTRY_SIZE);
    }
    kept++;
  }
  // Clear what is left after the kept entries.
  memset(table + (kept * LOOKUP_ENTRY_SIZE), 0,
         (size_t)(num_entries - kept) * LOOKUP_ENTRY_SIZE);
  *table_length = (uint16_t)(kept * LOOKUP_ENTRY_SIZE);
  return dropped;
}
