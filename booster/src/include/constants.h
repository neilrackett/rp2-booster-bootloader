/**
 * File: constants.h
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2024
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Constants used in the placeholder file
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "hardware/vreg.h"

// Base URL for firmware/version files (no trailing slash; can be overridden at
// compile time).
//
// https as of v2.3.0: the host terminates TLS 1.3 behind a CDN (cert
// CN=sidecartridge.com, Google Trust Services), verified 2026-07-29. This
// covers both VERSION_URL and FIRMWARE_BINARY_URL, so the firmware OTA now
// downloads over TLS as well. Note the TLS is encrypted but NOT authenticated
// (VERIFY_NONE) -- the MD5 check in appmngr remains the
// integrity check that actually gates flashing.
#ifndef SIDECART_BASE_URL
#define SIDECART_BASE_URL "https://atarist.sidecartridge.com"
#endif

// URL to fetch the firmware binary (can be overridden at compile time)
#ifndef FIRMWARE_BINARY_URL
#define FIRMWARE_BINARY_URL SIDECART_BASE_URL "/upgrade.bin"
#endif

// URL of the MD5 checksum for upgrade.bin. REQUIRED: the upgrade is refused if
// this file is missing, unreachable or malformed, so a release must always
// publish it alongside upgrade.bin (build.sh generates it).
#ifndef FIRMWARE_MD5_URL
#define FIRMWARE_MD5_URL SIDECART_BASE_URL "/upgrade.md5"
#endif

// URL to fetch the VERSION file (can be overridden at compile time)
#ifndef VERSION_URL
#define VERSION_URL SIDECART_BASE_URL "/SIDECARTVERSION"
#endif

// Common macros
#define HEX_BASE 16
#define DEC_BASE 10

// Time macros
#define SEC_TO_MS 1000

// SELECT signal
#define SELECT_GPIO 5  // GPIO signal for SELECT

// GPIO constants for the read address from the bus
#define READ_ADDR_GPIO_BASE 6     // Start of the GPIOs for the address
#define READ_ADDR_PIN_COUNT 16    // Number of GPIOs for the address
#define READ_SIGNAL_GPIO_BASE 27  // GPIO signal for READ
#define READ_SIGNAL_PIN_COUNT 1   // Number of GPIOs for the READ signal

// Write data to the bus
#define WRITE_DATA_GPIO_BASE \
  (READ_ADDR_GPIO_BASE)  // Start of the GPIOs for the data to write
#define WRITE_DATA_PIN_COUNT \
  (READ_ADDR_PIN_COUNT)  // Number of GPIOs for the data
#define WRITE_SIGNAL_GPIO_BASE \
  (READ_SIGNAL_GPIO_BASE + 1)  // GPIO signal for WRITE
#define WRITE_SIGNAL_PIN_COUNT 1

// FLASH and RAM sections constants.
#define ROM_BANKS 2  // Number of ROM banks to emulate
#define FLASH_ROM_LOAD_OFFSET \
  0xE0000  // Offset start in FLASH reserved for ROMs. Survives a reset or
           // poweroff.
#define FLASH_ROM4_LOAD_OFFSET FLASH_ROM_LOAD_OFFSET  // First 64KB block
#define FLASH_ROM3_LOAD_OFFSET \
  (FLASH_ROM_LOAD_OFFSET + 0x10000)              // Second 64KB block
#define ROM_SIZE_BYTES 0x10000                   // 64KBytes
#define ROM_SIZE_WORDS (ROM_SIZE_BYTES / 2)      // 32KWords
#define ROM_SIZE_LONGWORDS (ROM_SIZE_BYTES / 4)  // 16KLongWords

// Frequency constants.
#define SAMPLE_DIV_FREQ (1.f)         // Sample frequency division factor.
#define RP2040_CLOCK_FREQ_KHZ 225000  // Clock frequency in KHz (225MHz).

// Voltage constants.
#define RP2040_VOLTAGE VREG_VOLTAGE_1_20  // Voltage in 1.20 Volts.
#define VOLTAGE_VALUES                                                 \
  (const char *[]){"NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID", "NOT VALID", "0.85v",     "0.90v",     \
                   "0.95v",     "1.00v",     "1.05v",     "1.10v",     \
                   "1.15v",     "1.20v",     "1.25v",     "1.30v",     \
                   "NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID"}

#define BOOSTER_TITLE "SidecarTridge Multi-device"

#define CURRENT_APP_NAME_KEY "BOOSTER"

#define DEFAULT_APPS_FOLDER "/apps"

// Time macros
#define GET_CURRENT_TIME() \
  (((uint64_t)timer_hw->timerawh) << 32u | timer_hw->timerawl)
#define GET_CURRENT_TIME_INTERVAL_MS(start) \
  (uint32_t)((GET_CURRENT_TIME() - start) / \
             (((uint32_t)RP2040_CLOCK_FREQ_KHZ) / 1000))

// NOLINTBEGIN(readability-identifier-naming)
extern unsigned int __flash_binary_start;
extern unsigned int _storage_flash_start;
extern unsigned int _config_flash_start;
extern unsigned int _global_lookup_flash_start;
extern unsigned int _global_config_flash_start;
extern unsigned int __rom_in_ram_start__;
// NOLINTEND(readability-identifier-naming)

#endif  // CONSTANTS_H