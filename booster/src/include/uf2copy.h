/**
 * File: uf2copy.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Copy a microfirmware's UF2 file into the microfirmware slot
 */

#ifndef UF2COPY_H
#define UF2COPY_H

#include <stdint.h>

// The slot starts at the beginning of flash, so its first sector holds the
// second-stage bootloader (boot2) and the vector table the chip boots from.
// The copy first erases that sector alone and writes a stub there instead of
// the app: Booster's own boot2 and vector table, so a reset or a power cut at
// any later point of the copy boots Booster. It then erases the rest of the
// slot, writes the app from its second sector on, and writes the app's first
// sector last, read again from the file. Only those two erases and programs
// of the first sector, tens of milliseconds each, can still leave a slot that
// does not boot.
//
// The UF2 payloads are written one after the other from the start of the
// slot; block addresses are not used. Blocks with a bad magic or payload size
// are skipped, and a short block at the end of the file ends it.
#define UF2COPY_SECTOR_SIZE 4096u
#define UF2COPY_PAGE_SIZE 256u
#define UF2COPY_BLOCK_SIZE 512u
#define UF2COPY_BOOT2_SIZE 256u
// RP2040: 16 system exceptions and 32 interrupts.
#define UF2COPY_VECTORS_SIZE (48u * 4u)
#define UF2COPY_STUB_SIZE (2u * UF2COPY_PAGE_SIZE)

typedef enum {
  UF2COPY_OK = 0,
  // Bad arguments: nothing was erased, the slot holds what it held.
  UF2COPY_ERR_PARAM,
  // The file could not be read to the end: the slot boots Booster.
  UF2COPY_ERR_READ,
  // The file holds no UF2 payload: the slot boots Booster.
  UF2COPY_ERR_EMPTY,
  // The app is larger than the slot: the slot boots Booster.
  UF2COPY_ERR_TOO_BIG,
  // uf2copy_verifyRange(): flash differs from the file.
  UF2COPY_ERR_VERIFY
} uf2copy_result_t;

typedef struct {
  void *ctx;
  // Reads up to len bytes into buf and stores the count in *got, 0 at the end
  // of the file. Returns 0, or non-zero on a read error.
  int (*read)(void *ctx, uint8_t *buf, uint32_t len, uint32_t *got);
  // Goes back to the start of the file. Returns 0, or non-zero on an error.
  int (*rewind)(void *ctx);
  // Offsets are from the start of flash. erase takes whole sectors, program
  // whole pages.
  void (*erase)(void *ctx, uint32_t offset, uint32_t len);
  void (*program)(void *ctx, uint32_t offset, const uint8_t *data,
                  uint32_t len);
} uf2copy_io_t;

/**
 * @brief Builds the stub the slot boots while the copy runs.
 *
 * @param stub Out: UF2COPY_STUB_SIZE bytes, boot2 then the vector table, the
 * rest 0xFF.
 * @param boot2 UF2COPY_BOOT2_SIZE bytes of boot2, checksum included.
 * @param vectors UF2COPY_VECTORS_SIZE bytes of the vector table to boot.
 */
void uf2copy_buildStub(uint8_t *stub, const uint8_t *boot2,
                       const uint8_t *vectors);

/**
 * @brief Copies the file into the slot, the stub first and the app's first
 * sector last.
 *
 * @param io File and flash access.
 * @param offset Slot start, from the start of flash; a whole sector.
 * @param size Slot size; whole sectors.
 * @param stub UF2COPY_STUB_SIZE bytes from uf2copy_buildStub().
 * @param buf UF2COPY_SECTOR_SIZE bytes of working space.
 * @return UF2COPY_OK, or the reason the copy stopped.
 */
uf2copy_result_t uf2copy_run(const uf2copy_io_t *io, uint32_t offset,
                             uint32_t size, const uint8_t *stub, uint8_t *buf);

// The firmware upgrade works in parts of the file: the upgrader checks the
// whole file before it erases anything, writes and verifies Booster's region,
// and only then the start of the slot, where it runs from. These read the
// file from its start each time; payload offsets count UF2 payload bytes,
// which the image lays out from the start of flash.

/**
 * @brief Counts the UF2 payload bytes in the file, reading all of it.
 *
 * @return UF2COPY_OK with *total set, or UF2COPY_ERR_READ.
 */
uf2copy_result_t uf2copy_measure(const uf2copy_io_t *io, uint32_t *total);

/**
 * @brief Programs payload bytes [from, from + size) at offset, a sector at a
 * time. The caller erases first; payload past the range is not read.
 *
 * @param offset A whole sector, from the start of flash.
 * @param buf UF2COPY_SECTOR_SIZE bytes of working space.
 * @param written Out: the payload bytes programmed, fewer than size when the
 * file ends first.
 * @return UF2COPY_OK or UF2COPY_ERR_READ.
 */
uf2copy_result_t uf2copy_writeRange(const uf2copy_io_t *io, uint32_t from,
                                    uint32_t offset, uint32_t size,
                                    uint8_t *buf, uint32_t *written);

/**
 * @brief Reads payload bytes [from, from + size) into buf, size at most a
 * sector, so a sector can be read before it is erased.
 *
 * @param got Out: the bytes read, fewer than size when the file ends first.
 * @return UF2COPY_OK or UF2COPY_ERR_READ.
 */
uf2copy_result_t uf2copy_readRange(const uf2copy_io_t *io, uint32_t from,
                                   uint32_t size, uint8_t *buf, uint32_t *got);

/**
 * @brief Compares payload bytes [from, from + size) with flash.
 *
 * @param flash The flash those bytes were programmed to, readable.
 * @return UF2COPY_OK, UF2COPY_ERR_VERIFY on a difference or a file that ends
 * first, or UF2COPY_ERR_READ.
 */
uf2copy_result_t uf2copy_verifyRange(const uf2copy_io_t *io, uint32_t from,
                                     const uint8_t *flash, uint32_t size,
                                     uint8_t *buf);

/**
 * @brief Writes an image held in memory into the slot, in uf2copy_run()'s
 * order: the stub first and the image's first sector last.
 *
 * @param offset Slot start, from the start of flash; a whole sector.
 * @param image The image, size bytes.
 * @param buf UF2COPY_SECTOR_SIZE bytes of working space, for the padded last
 * sector.
 * @return UF2COPY_OK, or UF2COPY_ERR_PARAM with nothing written.
 */
uf2copy_result_t uf2copy_writeImage(const uf2copy_io_t *io, uint32_t offset,
                                    const uint8_t *image, uint32_t size,
                                    const uint8_t *stub, uint8_t *buf);

#endif  // UF2COPY_H
