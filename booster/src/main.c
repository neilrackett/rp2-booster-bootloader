/**
 * File: main.c
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that launches the ROM emulator
 */

#include "blink.h"
#include "constants.h"
#include "debug.h"
#include "fabric.h"
#include "gconfig.h"
#include "memfunc.h"
#include "mngr.h"
#include "network.h"
#include "reset.h"
#include "romemul.h"
#include "select.h"
#include "term.h"
#include "term_firmware.h"

#ifdef DISPLAY_ATARIST
#include "display.h"
#include "display_fabric.h"
#endif

int main() {
  // Set the clock frequency. 20% overclocking
  set_sys_clock_khz(RP2040_CLOCK_FREQ_KHZ, true);

  // Set the voltage
  vreg_set_voltage(RP2040_VOLTAGE);

#if defined(_DEBUG) && (_DEBUG != 0)
  // Initialize chosen serial port
  stdio_init_all();
  setvbuf(stdout, NULL, _IONBF,
          1);  // specify that the stream should be unbuffered

  // Only startup information to display
  DPRINTF("SidecarTridge Booster. %s (%s). %s mode.\n\n", RELEASE_VERSION,
          RELEASE_DATE, _DEBUG ? "DEBUG" : "RELEASE");

  // Show information about the frequency and voltage
  int current_clock_frequency_khz = RP2040_CLOCK_FREQ_KHZ;
  const char *current_voltage = VOLTAGE_VALUES[RP2040_VOLTAGE];
  DPRINTF("Clock frequency: %i KHz\n", current_clock_frequency_khz);
  DPRINTF("Voltage: %s\n", current_voltage);
  DPRINTF("PICO_FLASH_SIZE_BYTES: %i\n", PICO_FLASH_SIZE_BYTES);

  unsigned int flash_length =
      (unsigned int)&_config_flash_start - (unsigned int)&__flash_binary_start;
  unsigned int booster_flash_length = flash_length;
  unsigned int config_flash_length = (unsigned int)&_global_lookup_flash_start -
                                     (unsigned int)&_config_flash_start;
  unsigned int global_lookup_flash_length = FLASH_SECTOR_SIZE;
  unsigned int global_config_flash_length = FLASH_SECTOR_SIZE;
  unsigned int rom_in_ram_length = 64 * 1024;

  DPRINTF("Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&__flash_binary_start, flash_length);
  DPRINTF("Booster Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&__flash_binary_start, booster_flash_length);
  DPRINTF("Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_config_flash_start, config_flash_length);
  DPRINTF("Global Lookup Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_lookup_flash_start,
          global_lookup_flash_length);
  DPRINTF("Global Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_config_flash_start,
          global_config_flash_length);
  DPRINTF("ROM in RAM start: 0x%X, length: %u bytes\n",
          (unsigned int)&__rom_in_ram_start__, rom_in_ram_length);

#endif

  // Load the global configuration parameters
  int err = gconfig_init(NULL);
  if (err != GCONFIG_SUCCESS) {
    // Let's create the default configuration
    err = settings_save(gconfig_getContext(), true);
    if (err != 0) {
      DPRINTF("Error initializing the global configuration manager: %i\n", err);
      blink_error();
      return err;
    }
    // Deinit the settings
    settings_deinit(gconfig_getContext());

    // Start again with the default configuration
    int err = gconfig_init(NULL);
    if (err != GCONFIG_SUCCESS) {
      DPRINTF("Cannot initialize the global configuration manager: %i\n", err);
      blink_error();
      return err;
    }
    DPRINTF("Global configuration initialized\n");
  }

  // Migrate a canonical apps-catalog URL onto the md-store host. Runs
  // before ROM emulation starts, so the one-time flash write cannot disturb the
  // cartridge port, and before anything can serve the APPSURL SSI tag. A no-op
  // (and no flash write) for custom URLs and already-migrated devices.
  gconfig_migrateCatalogUrl();

  // Configure the input pins for SELECT button
  select_configure();
  select_coreWaitPush(reset_device, reset_deviceAndEraseFlash);

  // Copy the terminal firmware to RAM
  COPY_FIRMWARE_TO_RAM((uint16_t *)term_firmware, term_firmware_length * 2);

  // Init the terminal emulator
  term_init();

  // Register the term init callback, copy the FLASH ROMs to RAM, and start the
  // state machine
  init_romemul(NULL, term_dma_irq_handler_lookup, false);

  // Check if the boot feature is BOOSTER; if not, enter FABRIC mode
  SettingsConfigEntry *boot_feature =
      settings_find_entry(gconfig_getContext(), PARAM_BOOT_FEATURE);
  char *boot_feature_value = NULL;
  if (boot_feature == NULL) {
    DPRINTF("No boot feature found in the settings. Set FABRIC mode\n");
    boot_feature_value = "FABRIC";
  } else {
    DPRINTF("Boot feature: %s\n", boot_feature->value);
    if (strcmp(boot_feature->value, "FABRIC") == 0) {
      // BOOSTER Fabric configuration mode
      fabric_init();
      fabric_loop();
      return 0;
    }
    // BOOSTER Manager mode
    // Force to set the BOOSTER boot feature
    settings_put_string(gconfig_getContext(), PARAM_BOOT_FEATURE, "BOOSTER");
    settings_save(gconfig_getContext(), true);
    sleep_ms(100);  // Wait for the settings to be saved
    DPRINTF("Boot feature set to BOOSTER\n");
    multicore_lockout_victim_init();  // keep the core 1 locked out
    mngr_init();
    mngr_loop();
    return 0;
  }

  // Initialize the network
  // Get the WiFi mode from the settings
  SettingsConfigEntry *wifi_mode =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_MODE);
  wifi_mode_t wifi_mode_value = WIFI_MODE_AP;
  if (wifi_mode == NULL) {
    DPRINTF("No WiFi mode found in the settings. Using default AP mode\n");
  } else {
    wifi_mode_value = (wifi_mode_t)atoi(wifi_mode->value);
    if (wifi_mode_value != WIFI_MODE_AP) {
      DPRINTF("WiFi mode is STA\n");
      wifi_mode_value = WIFI_MODE_STA;
    } else {
      DPRINTF("WiFi mode is AP\n");
    }
  }

  err = network_wifiInit(wifi_mode_value);
  if (err != 0) {
    DPRINTF("Error initializing the network: %i\n", err);
    blink_error();
    return err;
  }

  // If STA mode, start connection to the WiFi network
  if (wifi_mode_value == WIFI_MODE_STA) {
    // Connect to the WiFi network
    err = NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;
    while (err == NETWORK_WIFI_STA_CONN_ERR_TIMEOUT) {
      err = network_wifiStaConnect();
      if ((err > 0) && (err < NETWORK_WIFI_STA_CONN_ERR_TIMEOUT)) {
        DPRINTF("Error connecting to the WiFi network: %i\n", err);
        blink_error();
        return err;
      }
    }
  } else {
    DPRINTF("WiFi mode is AP\n");
    blink_on();
  }

  //     // Start the HTTP server
  //     multicore_launch_core1(httpd_start);

  while (1) {
#if PICO_CYW43_ARCH_POLL
    network_safe_poll();
    absolute_time_t wait_until = make_timeout_time_ms(1000);
    cyw43_arch_wait_for_work_until(wait_until);
#else
    sleep_ms(1000);
#endif
    //    network_scan(&wifi_scan_time, wifi_scan_polling_interval);
  }
}
