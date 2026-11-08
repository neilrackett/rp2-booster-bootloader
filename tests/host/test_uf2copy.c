// units: booster/src/uf2copy.c
/* Copying a microfirmware's UF2 file into the slot: the bytes that land in
 * flash, every way the copy can fail, and the boot2-last order. After every
 * flash operation of a copy, a power cut must leave a slot whose first sector
 * holds the stub (Booster boots) or the whole app, except during the final
 * erase and program of that sector. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "uf2copy.h"

#define SLOT_SECTORS 4
#define SLOT_SIZE (SLOT_SECTORS * UF2COPY_SECTOR_SIZE)
#define MAX_OPS 32

typedef struct {
  const uint8_t *file;
  uint32_t fileSize;
  uint32_t pos;
  int reads;
  int failRead;  // fail the read with this number (1-based), 0 never
  bool failRewind;
  uint8_t flash[SLOT_SIZE];
  // The first sector after each flash operation, and what the operation was.
  uint8_t sector0[MAX_OPS][UF2COPY_SECTOR_SIZE];
  char op[MAX_OPS];
  int ops;
  // An erase that blanked the first sector together with others: a cut during
  // it leaves no boot2 for as long as the whole erase takes.
  bool longBlankFirstSector;
  bool badArgs;
} fake_t;

static int fakeRead(void *ctx, uint8_t *buf, uint32_t len, uint32_t *got) {
  fake_t *f = ctx;
  if (++f->reads == f->failRead) return 1;
  uint32_t n = f->fileSize - f->pos;
  if (n > len) n = len;
  memcpy(buf, f->file + f->pos, n);
  f->pos += n;
  *got = n;
  return 0;
}

static int fakeRewind(void *ctx) {
  fake_t *f = ctx;
  if (f->failRewind) return 1;
  f->pos = 0;
  return 0;
}

static void record(fake_t *f, char op) {
  if (f->ops < MAX_OPS) {
    memcpy(f->sector0[f->ops], f->flash, UF2COPY_SECTOR_SIZE);
    f->op[f->ops] = op;
  }
  f->ops++;
}

static void fakeErase(void *ctx, uint32_t offset, uint32_t len) {
  fake_t *f = ctx;
  if (offset % UF2COPY_SECTOR_SIZE || len % UF2COPY_SECTOR_SIZE ||
      offset + len > SLOT_SIZE) {
    f->badArgs = true;
    return;
  }
  if (offset == 0 && len > UF2COPY_SECTOR_SIZE) f->longBlankFirstSector = true;
  memset(f->flash + offset, 0xFF, len);
  record(f, 'E');
}

static void fakeProgram(void *ctx, uint32_t offset, const uint8_t *data,
                        uint32_t len) {
  fake_t *f = ctx;
  if (offset % UF2COPY_PAGE_SIZE || len % UF2COPY_PAGE_SIZE ||
      offset + len > SLOT_SIZE) {
    f->badArgs = true;
    return;
  }
  for (uint32_t i = 0; i < len; i++) {
    if (f->flash[offset + i] != 0xFF) f->badArgs = true;  // not erased
    f->flash[offset + i] = data[i];
  }
  record(f, 'P');
}

static uint8_t stub[UF2COPY_STUB_SIZE];
static uint8_t workBuf[UF2COPY_SECTOR_SIZE];

// A UF2 file whose payload is `payload` bytes of a known pattern, `perBlock`
// bytes per block; *size gets the file size.
static uint8_t *makeUf2(uint32_t payload, uint32_t perBlock, uint32_t *size) {
  uint32_t blocks = (payload + perBlock - 1) / perBlock;
  uint8_t *file = calloc(blocks ? blocks : 1, UF2COPY_BLOCK_SIZE);
  for (uint32_t b = 0; b < blocks; b++) {
    uint8_t *blk = file + b * UF2COPY_BLOCK_SIZE;
    uint32_t n = payload - b * perBlock;
    if (n > perBlock) n = perBlock;
    const uint32_t words[5] = {0x0A324655u, 0x9E5D5157u, 0,
                               0x10000000u + b * 256, n};
    for (int w = 0; w < 5; w++) {
      for (int i = 0; i < 4; i++)
        blk[w * 4 + i] = (uint8_t)(words[w] >> (8 * i));
    }
    for (uint32_t i = 0; i < n; i++) {
      blk[32 + i] = (uint8_t)((b * perBlock + i) * 13 + 1);
    }
  }
  *size = blocks * UF2COPY_BLOCK_SIZE;
  return file;
}

static bool payloadAt(const fake_t *f, uint32_t payload) {
  for (uint32_t i = 0; i < SLOT_SIZE; i++) {
    uint8_t want = i < payload ? (uint8_t)(i * 13 + 1) : 0xFF;
    if (f->flash[i] != want) return false;
  }
  return true;
}

static bool isStub(const uint8_t *sector) {
  if (memcmp(sector, stub, UF2COPY_STUB_SIZE) != 0) return false;
  for (uint32_t i = UF2COPY_STUB_SIZE; i < UF2COPY_SECTOR_SIZE; i++) {
    if (sector[i] != 0xFF) return false;
  }
  return true;
}

static uf2copy_result_t run(fake_t *f, const uint8_t *file, uint32_t size) {
  f->file = file;
  f->fileSize = size;
  uf2copy_io_t io = {f, fakeRead, fakeRewind, fakeErase, fakeProgram};
  return uf2copy_run(&io, 0, SLOT_SIZE, stub, workBuf);
}

int main(void) {
  uint8_t boot2[UF2COPY_BOOT2_SIZE];
  uint8_t vectors[UF2COPY_VECTORS_SIZE];
  memset(boot2, 0xB2, sizeof(boot2));
  memset(vectors, 0x7E, sizeof(vectors));
  uf2copy_buildStub(stub, boot2, vectors);
  CHECK(memcmp(stub, boot2, sizeof(boot2)) == 0);
  CHECK(memcmp(stub + 256, vectors, sizeof(vectors)) == 0);
  CHECK_EQ(stub[256 + UF2COPY_VECTORS_SIZE], 0xFF);
  CHECK_EQ(stub[UF2COPY_STUB_SIZE - 1], 0xFF);

  fake_t *f = calloc(1, sizeof(fake_t));
  uint32_t size = 0;

  // Two and a half sectors in 256-byte payloads, as the SDK writes them.
  uint32_t payload = 2 * UF2COPY_SECTOR_SIZE + 2048;
  uint8_t *file = makeUf2(payload, 256, &size);
  CHECK_EQ(run(f, file, size), UF2COPY_OK);
  CHECK(payloadAt(f, payload));
  CHECK(!f->badArgs);
  // Sector 0 erased, the stub, the rest erased, sectors 1 and 2, then sector
  // 0 erased and programmed last.
  CHECK_EQ(f->ops, 7);
  CHECK(memcmp(f->op, "EPEPPEP", 7) == 0);
  CHECK(!f->longBlankFirstSector);
  // A cut after any operation but the last erase boots Booster.
  for (int i = 1; i < f->ops - 2; i++) CHECK(isStub(f->sector0[i]));
  free(file);

  // Payloads of 476 bytes straddle the sector boundaries.
  memset(f, 0, sizeof(*f));
  payload = 3 * UF2COPY_SECTOR_SIZE + 100;
  file = makeUf2(payload, 476, &size);
  CHECK_EQ(run(f, file, size), UF2COPY_OK);
  CHECK(payloadAt(f, payload));
  CHECK(!f->badArgs);
  free(file);

  // An app smaller than a sector.
  memset(f, 0, sizeof(*f));
  payload = 300;
  file = makeUf2(payload, 256, &size);
  CHECK_EQ(run(f, file, size), UF2COPY_OK);
  CHECK(payloadAt(f, payload));
  CHECK(memcmp(f->op, "EPEEP", 5) == 0);
  free(file);

  // A block with a bad magic is skipped; a short block at the end is ignored.
  memset(f, 0, sizeof(*f));
  payload = 2 * 256;
  file = makeUf2(3 * 256, 256, &size);
  memmove(file + 2 * UF2COPY_BLOCK_SIZE, file + UF2COPY_BLOCK_SIZE,
          UF2COPY_BLOCK_SIZE);
  file[UF2COPY_BLOCK_SIZE] ^= 1;                 // block 1 now has a bad magic
  CHECK_EQ(run(f, file, size - 1), UF2COPY_OK);  // and block 2 is short
  // Only block 0 lands: block 1 skipped, block 2 cut short.
  CHECK(payloadAt(f, 256));
  free(file);

  // A read error at every point of a copy leaves the stub, never a part of
  // the app, in the first sector.
  payload = 2 * UF2COPY_SECTOR_SIZE + 2048;
  file = makeUf2(payload, 256, &size);
  int blocks = (int)(size / UF2COPY_BLOCK_SIZE);
  for (int n = 1; n <= blocks + 1; n++) {
    memset(f, 0, sizeof(*f));
    f->failRead = n;
    CHECK_EQ(run(f, file, size), UF2COPY_ERR_READ);
    CHECK(isStub(f->flash));
  }
  // The same during the second read of the first sector.
  memset(f, 0, sizeof(*f));
  f->failRead = blocks + 2;
  CHECK_EQ(run(f, file, size), UF2COPY_ERR_READ);
  CHECK(isStub(f->flash));
  memset(f, 0, sizeof(*f));
  f->failRewind = true;
  CHECK_EQ(run(f, file, size), UF2COPY_ERR_READ);
  CHECK(isStub(f->flash));
  free(file);

  // No payload at all.
  memset(f, 0, sizeof(*f));
  static const uint8_t noFile[1];
  CHECK_EQ(run(f, noFile, 0), UF2COPY_ERR_EMPTY);
  CHECK(isStub(f->flash));

  // Larger than the slot.
  memset(f, 0, sizeof(*f));
  file = makeUf2(SLOT_SIZE + 1, 256, &size);
  CHECK_EQ(run(f, file, size), UF2COPY_ERR_TOO_BIG);
  CHECK(isStub(f->flash));
  free(file);
  // Exactly the slot fits.
  memset(f, 0, sizeof(*f));
  file = makeUf2(SLOT_SIZE, 256, &size);
  CHECK_EQ(run(f, file, size), UF2COPY_OK);
  CHECK(payloadAt(f, SLOT_SIZE));
  free(file);

  // Bad arguments touch nothing.
  memset(f, 0, sizeof(*f));
  uf2copy_io_t io = {f, fakeRead, fakeRewind, fakeErase, fakeProgram};
  CHECK_EQ(uf2copy_run(&io, 100, SLOT_SIZE, stub, workBuf), UF2COPY_ERR_PARAM);
  CHECK_EQ(uf2copy_run(&io, 0, 100, stub, workBuf), UF2COPY_ERR_PARAM);
  CHECK_EQ(uf2copy_run(&io, 0, 0, stub, workBuf), UF2COPY_ERR_PARAM);
  CHECK_EQ(f->ops, 0);

  free(f);
  TEST_END();
}
