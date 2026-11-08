/**
 * File: lookupbackup.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The copy of the apps lookup table kept on the microSD card
 */

#include "lookupbackup.h"

#include <string.h>

#include "lookuptable.h"

static const uint8_t LOOKUPBACKUP_MAGIC[4] = {'B', 'K', 'L', 'T'};
enum { LOOKUPBACKUP_VERSION = 1 };

uint32_t lookupbackup_crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

static void put16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value & 0xFF);
  out[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *out, uint32_t value) {
  for (int i = 0; i < 4; i++) {
    out[i] = (uint8_t)(value >> (8 * i));
  }
}

static uint32_t get32(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}

size_t lookupbackup_encode(uint8_t *out, const uint8_t *table,
                           uint16_t table_length,
                           const uint8_t device_id[LOOKUPBACKUP_ID_SIZE]) {
  memcpy(out, LOOKUPBACKUP_MAGIC, sizeof(LOOKUPBACKUP_MAGIC));
  put16(out + 4, LOOKUPBACKUP_VERSION);
  put16(out + 6, table_length);
  memcpy(out + 8, device_id, LOOKUPBACKUP_ID_SIZE);
  put32(out + 16, lookupbackup_crc32(table, table_length));
  memcpy(out + LOOKUPBACKUP_HEADER_SIZE, table, table_length);
  return LOOKUPBACKUP_HEADER_SIZE + (size_t)table_length;
}

int lookupbackup_validate(const uint8_t *data, size_t data_length,
                          const uint8_t device_id[LOOKUPBACKUP_ID_SIZE],
                          uint16_t max_table) {
  if (data_length < LOOKUPBACKUP_HEADER_SIZE ||
      memcmp(data, LOOKUPBACKUP_MAGIC, sizeof(LOOKUPBACKUP_MAGIC)) != 0) {
    return -1;
  }
  uint16_t version = (uint16_t)(data[4] | (data[5] << 8));
  uint16_t length = (uint16_t)(data[6] | (data[7] << 8));
  if (version != LOOKUPBACKUP_VERSION || length > max_table ||
      length % LOOKUP_ENTRY_SIZE != 0 ||
      data_length != LOOKUPBACKUP_HEADER_SIZE + (size_t)length) {
    return -1;
  }
  if (memcmp(data + 8, device_id, LOOKUPBACKUP_ID_SIZE) != 0) {
    return -1;
  }
  if (get32(data + 16) !=
      lookupbackup_crc32(data + LOOKUPBACKUP_HEADER_SIZE, length)) {
    return -1;
  }
  return length;
}
