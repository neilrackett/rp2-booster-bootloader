/**
 * File: lookupbackup.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The copy of the apps lookup table kept on the microSD card
 */

#ifndef LOOKUPBACKUP_H
#define LOOKUPBACKUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The lookup table lives in one flash sector, rewritten by erase then
// program: a power cut in between leaves it empty or cut short. Before every
// rewrite the new table is saved to the card in this format, and at boot a
// valid copy that differs from flash is programmed back.
//
//   0  4  magic "BKLT"
//   4  2  format version (1), little-endian
//   6  2  table length in bytes, little-endian, a multiple of the entry size
//   8  8  the device's unique board ID: a copy from another device is ignored
//  16  4  CRC-32 of the table bytes, little-endian
//  20     the table
#define LOOKUPBACKUP_HEADER_SIZE 20
#define LOOKUPBACKUP_ID_SIZE 8

/**
 * @brief Writes the header and the table into out (at least
 * LOOKUPBACKUP_HEADER_SIZE + table_length bytes).
 *
 * @return The number of bytes written.
 */
size_t lookupbackup_encode(uint8_t *out, const uint8_t *table,
                           uint16_t table_length,
                           const uint8_t device_id[LOOKUPBACKUP_ID_SIZE]);

/**
 * @brief Checks a backup read from the card: magic, version, a length that
 * fits max_table and is a whole number of entries, the CRC, and that it was
 * written by this device.
 *
 * @return The table length when valid (the table starts at
 * data + LOOKUPBACKUP_HEADER_SIZE), or -1.
 */
int lookupbackup_validate(const uint8_t *data, size_t data_length,
                          const uint8_t device_id[LOOKUPBACKUP_ID_SIZE],
                          uint16_t max_table);

/**
 * @brief CRC-32 (IEEE 802.3, the zlib one) of data.
 */
uint32_t lookupbackup_crc32(const uint8_t *data, size_t length);

#endif  // LOOKUPBACKUP_H
