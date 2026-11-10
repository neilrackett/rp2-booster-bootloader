/**
 * File: main.c
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that launches the ROM emulator
 */

#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hw_config.h"
#include "sd_card.h"
#include "uf2copy.h"

#define UPGRADE_FILENAME "upgrade.bin"

static uint8_t sector_buf[UF2COPY_SECTOR_SIZE];

static size_t bounded_copy(char *dst, size_t dst_len, const char *src) {
  size_t i = 0;

  if (dst_len == 0) {
    return 0;
  }

  while ((i + 1 < dst_len) && (src[i] != '\0')) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';

  return i;
}

static FRESULT build_upgrade_path(char *dst, size_t dst_len,
                                  const char *apps_folder) {
  const char suffix[] = UPGRADE_FILENAME;
  size_t path_len = bounded_copy(dst, dst_len, apps_folder);
  size_t suffix_idx = 0;

  if ((path_len == 0) || (dst[path_len] != '\0')) {
    return FR_INVALID_NAME;
  }

  if (dst[path_len - 1] != '/') {
    if (path_len + 1 >= dst_len) {
      return FR_INVALID_NAME;
    }
    dst[path_len++] = '/';
    dst[path_len] = '\0';
  }

  while (suffix[suffix_idx] != '\0') {
    if (path_len + 1 >= dst_len) {
      return FR_INVALID_NAME;
    }
    dst[path_len++] = suffix[suffix_idx++];
  }
  dst[path_len] = '\0';

  return FR_OK;
}

static inline void jump_to_booster_app() {
  // This code jumps to the Booster application at the top of the flash memory
  // The reason to perform this jump is for performance reasons
  // This code should be placed at the beginning of the main function, and
  // should be executed if the SELECT signal or the BOOSTER app is selected Set
  // VTOR register, set stack pointer, and jump to reset
  __asm__ __volatile__(
      "mov r0, %[start]\n"
      "ldr r1, =%[vtable]\n"
      "str r0, [r1]\n"
      "ldmia r0, {r0, r1}\n"
      "msr msp, r0\n"
      "bx r1\n"
      :
      : [start] "r"((unsigned int)&_booster_app_flash_start + 256),
        [vtable] "X"(PPB_BASE + M0PLUS_VTOR_OFFSET)
      :);
  DPRINTF("You should never reach this point\n");
}

// FatFs and flash access for the uf2copy functions. This upgrader runs from
// RAM, so flash can be read between two erases or programs.
typedef struct {
  FIL file;
  const char *path;
} upgrade_file_t;

static int upgrade_read(void *ctx, uint8_t *buf, uint32_t len, uint32_t *got) {
  UINT n = 0;
  if (f_read(&((upgrade_file_t *)ctx)->file, buf, len, &n) != FR_OK) {
    return 1;
  }
  *got = n;
  return 0;
}

// FatFs is built without f_lseek() to keep the upgrader small: start over by
// opening the file again.
static int upgrade_rewind(void *ctx) {
  upgrade_file_t *f = (upgrade_file_t *)ctx;
  f_close(&f->file);
  return f_open(&f->file, f->path, FA_READ) == FR_OK ? 0 : 1;
}

static void upgrade_erase(void *ctx, uint32_t offset, uint32_t len) {
  (void)ctx;
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(offset, len);
  restore_interrupts(ints);
}

static void upgrade_program(void *ctx, uint32_t offset, const uint8_t *data,
                            uint32_t len) {
  (void)ctx;
  uint32_t ints = save_and_disable_interrupts();
  flash_range_program(offset, data, len);
  restore_interrupts(ints);
}

// Booster's region is gone or unverified: stop here. The slot, where this
// upgrader lives, is untouched and the boot target is still the upgrader, so
// the next power-on runs it again and retries the copy from the card.
static void upgrade_failed(const char *why) {
  DPRINTF("Upgrade failed: %s. Power-cycle to try again.\n", why);
  while (true) {
    __wfi();
  }
}

// Writes upgrade.bin, the full image laid out from the start of flash: the
// placeholder's slot, then Booster. Booster's region goes first and is read
// back, while the slot still holds this upgrader; the slot goes last, in the
// launch copy's order, with the new Booster as its stub.
static void upgrade(const char *path) {
  const uint32_t booster = (uint32_t)&_booster_app_flash_start - XIP_BASE;
  const uint32_t end = (uint32_t)&_config_flash_start - XIP_BASE;

  static upgrade_file_t file;
  file.path = path;
  FRESULT fres = f_open(&file.file, path, FA_READ);
  if (fres != FR_OK) {
    DPRINTF("Cannot open %s: %d\n", path, fres);
    return;
  }
  uf2copy_io_t io = {&file, upgrade_read, upgrade_rewind, upgrade_erase,
                     upgrade_program};

  // The whole file once, before anything is erased: a card that cannot read
  // it, or a file that is not a full image, leaves Booster as it is.
  uint32_t total = 0;
  uf2copy_result_t measured = uf2copy_measure(&io, &total);
  // Images built before v2.5.0 carry one byte past the end, at the first
  // config sector's address; the region is all that is written.
  if (total > end) {
    total = end;
  }
  if (measured != UF2COPY_OK || total <= booster) {
    DPRINTF("Not a full image (%lu payload bytes), or unreadable\n",
            (unsigned long)total);
    f_close(&file.file);
    return;
  }

  DPRINTF("Writing Booster: %lu bytes\n", (unsigned long)(total - booster));
  upgrade_erase(NULL, booster, end - booster);
  uint32_t written = 0;
  if (uf2copy_writeRange(&io, booster, booster, end - booster, sector_buf,
                         &written) != UF2COPY_OK) {
    upgrade_failed("cannot read the image");
  }
  if (uf2copy_verifyRange(&io, booster, (const uint8_t *)XIP_BASE + booster,
                          total - booster, sector_buf) != UF2COPY_OK) {
    upgrade_failed("Booster differs from the image");
  }

  // The new Booster is in place. The slot: its first sector boots that
  // Booster while the rest is written, and is written last.
  DPRINTF("Writing the slot\n");
  uint8_t stub[UF2COPY_STUB_SIZE];
  const uint8_t *newBooster = (const uint8_t *)XIP_BASE + booster;
  uf2copy_buildStub(stub, newBooster, newBooster + UF2COPY_BOOT2_SIZE);
  upgrade_erase(NULL, 0, UF2COPY_SECTOR_SIZE);
  upgrade_program(NULL, 0, stub, UF2COPY_STUB_SIZE);
  upgrade_erase(NULL, UF2COPY_SECTOR_SIZE, booster - UF2COPY_SECTOR_SIZE);
  if (uf2copy_writeRange(&io, UF2COPY_SECTOR_SIZE, UF2COPY_SECTOR_SIZE,
                         booster - UF2COPY_SECTOR_SIZE, sector_buf,
                         &written) == UF2COPY_OK) {
    uint32_t got = 0;
    if (uf2copy_readRange(&io, 0, UF2COPY_SECTOR_SIZE, sector_buf, &got) ==
            UF2COPY_OK &&
        got > 0) {
      upgrade_erase(NULL, 0, UF2COPY_SECTOR_SIZE);
      uint32_t len = (got + UF2COPY_PAGE_SIZE - 1) & ~(UF2COPY_PAGE_SIZE - 1);
      memset(sector_buf + got, 0xFF, len - got);
      upgrade_program(NULL, 0, sector_buf, len);
    }
  }
  // A failure in the slot leaves the stub, which boots the new Booster.
  f_close(&file.file);
  DPRINTF("Upgrade written\n");
}

int main() {
#if defined(_DEBUG) && (_DEBUG != 0)
  // Initialize chosen serial port
  stdio_init_all();
  setvbuf(stdout, NULL, _IONBF,
          1);  // specify that the stream should be unbuffered

  // Only startup information to display
  DPRINTF(
      "\n\nUpgrader: upgrade the firmware in the device. %s (%s). %s mode.\n\n",
      RELEASE_VERSION, RELEASE_DATE, _DEBUG ? "DEBUG" : "RELEASE");

  // Show information about the frequency and voltage
  int current_clock_frequency_khz = RP2040_CLOCK_FREQ_KHZ;
  const char *current_voltage = VOLTAGE_VALUES[RP2040_VOLTAGE];
  DPRINTF("Clock frequency: %i KHz\n", current_clock_frequency_khz);
  DPRINTF("Voltage: %s\n", current_voltage);
  DPRINTF("PICO_FLASH_SIZE_BYTES: %i\n", PICO_FLASH_SIZE_BYTES);

  unsigned int flash_length = (unsigned int)&_booster_app_flash_start -
                              (unsigned int)&__flash_binary_start;
  unsigned int booster_flash_length = (unsigned int)&_config_flash_start -
                                      (unsigned int)&_booster_app_flash_start;
  unsigned int config_flash_length = (unsigned int)&_global_lookup_flash_start -
                                     (unsigned int)&_config_flash_start;
  unsigned int global_lookup_flash_length = FLASH_SECTOR_SIZE;
  unsigned int global_config_flash_length = FLASH_SECTOR_SIZE;

  DPRINTF("Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&__flash_binary_start, flash_length);
  DPRINTF("Booster Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_booster_app_flash_start, booster_flash_length);
  DPRINTF("Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_config_flash_start, config_flash_length);
  DPRINTF("Global Lookup Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_lookup_flash_start,
          global_lookup_flash_length);
  DPRINTF("Global Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_config_flash_start,
          global_config_flash_length);

#endif

  // We don't want to initialized any settings in the global config
  // We only want to read the global config to get the magic number and version
  // number So we don't pass any entries to the settings_init function
  int err = gconfig_init(CURRENT_APP_UUID_KEY);

  if (err < 0) {
    DPRINTF("Settings not initialized. Jump to Booster application\n");
    jump_to_booster_app();
  }

  DPRINTF("Start the upgrade here\n");

  // Create the temporary name of the file downloaded
  char tmp_binary_filename[256] = {0};
  if (build_upgrade_path(tmp_binary_filename, sizeof(tmp_binary_filename),
                         gconfig_get_apps_folder()) != FR_OK) {
    DPRINTF("Invalid upgrade filename path\n");
    jump_to_booster_app();
  }

  // The SD card speed set in Booster, lowered there for a card that needs it.
  unsigned int baudKb = gconfig_get_sd_baud_rate_kb();
  if (baudKb > 0 && sd_get_num() > 0) {
    DPRINTF("SD card baud rate: %u kbit/s\n", baudKb);
    sd_get_by_num(0)->spi_if_p->spi->baud_rate = baudKb * 1000U;
  }

  // We must initialize the FatFS file system
  FATFS fs;
  FRESULT res = f_mount(&fs, "", 1);
  if (res != FR_OK) {
    DPRINTF("Failed to mount FatFS: %i\n", res);
    jump_to_booster_app();
  }

  // Now the upgrade.bin file is the firmware binary, it must be copied to
  // the start of the flash
  DPRINTF("Writing firmware binary to flash: %s\n", tmp_binary_filename);
  upgrade(tmp_binary_filename);

  DPRINTF("Launching firmware binary\n");
  jump_to_booster_app();
  DPRINTF("You should never reach this point\n");

  while (1) {
    // Do something
  }
}
