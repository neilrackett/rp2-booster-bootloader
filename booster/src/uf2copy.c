/**
 * File: uf2copy.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Copy a microfirmware's UF2 file into the microfirmware slot
 */

#include "uf2copy.h"

#include <stdbool.h>
#include <string.h>

#define UF2_MAGIC_START0 0x0A324655u
#define UF2_MAGIC_START1 0x9E5D5157u
#define UF2_PAYLOAD_OFFSET 32u
#define UF2_PAYLOAD_MAX 476u

// The payload of the UF2 block being consumed.
typedef struct {
  uint8_t block[UF2COPY_BLOCK_SIZE];
  uint32_t pos;
  uint32_t len;
  bool eof;
} uf2copy_reader_t;

static uint32_t readLE32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// Loads the next block with a valid header. Returns false on a read error;
// at the end of the file it sets eof.
static bool nextBlock(const uf2copy_io_t *io, uf2copy_reader_t *r) {
  for (;;) {
    uint32_t got = 0;
    if (io->read(io->ctx, r->block, UF2COPY_BLOCK_SIZE, &got) != 0) {
      return false;
    }
    if (got < UF2COPY_BLOCK_SIZE) {
      r->eof = true;
      return true;
    }
    uint32_t payloadSize = readLE32(r->block + 16);
    if (readLE32(r->block) != UF2_MAGIC_START0 ||
        readLE32(r->block + 4) != UF2_MAGIC_START1 || payloadSize == 0 ||
        payloadSize > UF2_PAYLOAD_MAX) {
      continue;
    }
    r->pos = UF2_PAYLOAD_OFFSET;
    r->len = UF2_PAYLOAD_OFFSET + payloadSize;
    return true;
  }
}

// Fills buf with the next sector of payload; *filled is less than a sector
// only at the end of the file.
static uf2copy_result_t fillSector(const uf2copy_io_t *io, uf2copy_reader_t *r,
                                   uint8_t *buf, uint32_t *filled) {
  *filled = 0;
  while (*filled < UF2COPY_SECTOR_SIZE) {
    if (r->pos == r->len) {
      if (r->eof) {
        break;
      }
      if (!nextBlock(io, r)) {
        return UF2COPY_ERR_READ;
      }
      continue;
    }
    uint32_t n = r->len - r->pos;
    if (n > UF2COPY_SECTOR_SIZE - *filled) {
      n = UF2COPY_SECTOR_SIZE - *filled;
    }
    memcpy(buf + *filled, r->block + r->pos, n);
    r->pos += n;
    *filled += n;
  }
  return UF2COPY_OK;
}

// Pads a partial sector with erased bytes to whole pages; returns its length.
static uint32_t padToPage(uint8_t *buf, uint32_t filled) {
  uint32_t len = (filled + UF2COPY_PAGE_SIZE - 1) & ~(UF2COPY_PAGE_SIZE - 1);
  memset(buf + filled, 0xFF, len - filled);
  return len;
}

void uf2copy_buildStub(uint8_t *stub, const uint8_t *boot2,
                       const uint8_t *vectors) {
  memset(stub, 0xFF, UF2COPY_STUB_SIZE);
  memcpy(stub, boot2, UF2COPY_BOOT2_SIZE);
  memcpy(stub + UF2COPY_BOOT2_SIZE, vectors, UF2COPY_VECTORS_SIZE);
}

uf2copy_result_t uf2copy_run(const uf2copy_io_t *io, uint32_t offset,
                             uint32_t size, const uint8_t *stub, uint8_t *buf) {
  if (io == NULL || stub == NULL || buf == NULL || size == 0 ||
      (offset % UF2COPY_SECTOR_SIZE) != 0 ||
      (size % UF2COPY_SECTOR_SIZE) != 0) {
    return UF2COPY_ERR_PARAM;
  }

  // The first sector alone, then the stub, then the rest of the slot: erasing
  // the whole slot takes seconds, and the first sector must not stay blank
  // that long.
  io->erase(io->ctx, offset, UF2COPY_SECTOR_SIZE);
  io->program(io->ctx, offset, stub, UF2COPY_STUB_SIZE);
  if (size > UF2COPY_SECTOR_SIZE) {
    io->erase(io->ctx, offset + UF2COPY_SECTOR_SIZE,
              size - UF2COPY_SECTOR_SIZE);
  }

  // Everything but the first sector.
  uf2copy_reader_t reader = {0};
  uint32_t at = offset;
  uint32_t total = 0;
  for (;;) {
    uint32_t filled = 0;
    uf2copy_result_t res = fillSector(io, &reader, buf, &filled);
    if (res != UF2COPY_OK) {
      return res;
    }
    if (filled == 0) {
      break;
    }
    if (at >= offset + size) {
      return UF2COPY_ERR_TOO_BIG;
    }
    if (at != offset) {
      io->program(io->ctx, at, buf, padToPage(buf, filled));
    }
    total += filled;
    at += UF2COPY_SECTOR_SIZE;
    if (filled < UF2COPY_SECTOR_SIZE) {
      break;
    }
  }
  if (total == 0) {
    return UF2COPY_ERR_EMPTY;
  }

  // The first sector last, read again from the file.
  if (io->rewind(io->ctx) != 0) {
    return UF2COPY_ERR_READ;
  }
  uf2copy_reader_t again = {0};
  uint32_t filled = 0;
  uf2copy_result_t res = fillSector(io, &again, buf, &filled);
  if (res != UF2COPY_OK) {
    return res;
  }
  if (filled == 0) {
    return UF2COPY_ERR_READ;
  }
  io->erase(io->ctx, offset, UF2COPY_SECTOR_SIZE);
  io->program(io->ctx, offset, buf, padToPage(buf, filled));
  return UF2COPY_OK;
}

// Skips n payload bytes. Returns false on a read error; a file that ends first
// leaves the reader at its end.
static bool skipPayload(const uf2copy_io_t *io, uf2copy_reader_t *r,
                        uint32_t n) {
  while (n > 0) {
    if (r->pos == r->len) {
      if (r->eof) {
        return true;
      }
      if (!nextBlock(io, r)) {
        return false;
      }
      continue;
    }
    uint32_t step = r->len - r->pos;
    if (step > n) {
      step = n;
    }
    r->pos += step;
    n -= step;
  }
  return true;
}

uf2copy_result_t uf2copy_measure(const uf2copy_io_t *io, uint32_t *total) {
  *total = 0;
  if (io->rewind(io->ctx) != 0) {
    return UF2COPY_ERR_READ;
  }
  uf2copy_reader_t reader = {0};
  for (;;) {
    if (!nextBlock(io, &reader)) {
      return UF2COPY_ERR_READ;
    }
    if (reader.eof) {
      return UF2COPY_OK;
    }
    *total += reader.len - reader.pos;
  }
}

uf2copy_result_t uf2copy_writeRange(const uf2copy_io_t *io, uint32_t from,
                                    uint32_t offset, uint32_t size,
                                    uint8_t *buf, uint32_t *written) {
  *written = 0;
  if (io->rewind(io->ctx) != 0) {
    return UF2COPY_ERR_READ;
  }
  uf2copy_reader_t reader = {0};
  if (!skipPayload(io, &reader, from)) {
    return UF2COPY_ERR_READ;
  }
  while (*written < size) {
    uint32_t filled = 0;
    uf2copy_result_t res = fillSector(io, &reader, buf, &filled);
    if (res != UF2COPY_OK) {
      return res;
    }
    if (filled > size - *written) {
      filled = size - *written;
    }
    if (filled == 0) {
      break;
    }
    io->program(io->ctx, offset + *written, buf, padToPage(buf, filled));
    *written += filled;
    if (filled < UF2COPY_SECTOR_SIZE) {
      break;
    }
  }
  return UF2COPY_OK;
}

uf2copy_result_t uf2copy_readRange(const uf2copy_io_t *io, uint32_t from,
                                   uint32_t size, uint8_t *buf, uint32_t *got) {
  *got = 0;
  if (size > UF2COPY_SECTOR_SIZE) {
    return UF2COPY_ERR_PARAM;
  }
  if (io->rewind(io->ctx) != 0) {
    return UF2COPY_ERR_READ;
  }
  uf2copy_reader_t reader = {0};
  if (!skipPayload(io, &reader, from)) {
    return UF2COPY_ERR_READ;
  }
  uf2copy_result_t res = fillSector(io, &reader, buf, got);
  if (*got > size) {
    *got = size;
  }
  return res;
}

uf2copy_result_t uf2copy_verifyRange(const uf2copy_io_t *io, uint32_t from,
                                     const uint8_t *flash, uint32_t size,
                                     uint8_t *buf) {
  if (io->rewind(io->ctx) != 0) {
    return UF2COPY_ERR_READ;
  }
  uf2copy_reader_t reader = {0};
  if (!skipPayload(io, &reader, from)) {
    return UF2COPY_ERR_READ;
  }
  uint32_t checked = 0;
  while (checked < size) {
    uint32_t filled = 0;
    uf2copy_result_t res = fillSector(io, &reader, buf, &filled);
    if (res != UF2COPY_OK) {
      return res;
    }
    if (filled > size - checked) {
      filled = size - checked;
    }
    if (filled == 0) {
      return UF2COPY_ERR_VERIFY;  // the file ends before the range does
    }
    if (memcmp(buf, flash + checked, filled) != 0) {
      return UF2COPY_ERR_VERIFY;
    }
    checked += filled;
  }
  return UF2COPY_OK;
}

uf2copy_result_t uf2copy_writeImage(const uf2copy_io_t *io, uint32_t offset,
                                    const uint8_t *image, uint32_t size,
                                    const uint8_t *stub, uint8_t *buf) {
  if (io == NULL || image == NULL || stub == NULL || buf == NULL || size == 0 ||
      (offset % UF2COPY_SECTOR_SIZE) != 0) {
    return UF2COPY_ERR_PARAM;
  }
  uint32_t sectors = (size + UF2COPY_SECTOR_SIZE - 1) / UF2COPY_SECTOR_SIZE;

  io->erase(io->ctx, offset, UF2COPY_SECTOR_SIZE);
  io->program(io->ctx, offset, stub, UF2COPY_STUB_SIZE);
  if (sectors > 1) {
    io->erase(io->ctx, offset + UF2COPY_SECTOR_SIZE,
              (sectors - 1) * UF2COPY_SECTOR_SIZE);
  }
  // Every sector but the first, then the first.
  for (uint32_t k = 1; k <= sectors; k++) {
    uint32_t sector = k % sectors;
    if (sector == 0) {
      io->erase(io->ctx, offset, UF2COPY_SECTOR_SIZE);
    }
    uint32_t at = sector * UF2COPY_SECTOR_SIZE;
    uint32_t n = size - at;
    if (n > UF2COPY_SECTOR_SIZE) {
      n = UF2COPY_SECTOR_SIZE;
    }
    memcpy(buf, image + at, n);
    io->program(io->ctx, offset + at, buf, padToPage(buf, n));
  }
  return UF2COPY_OK;
}
