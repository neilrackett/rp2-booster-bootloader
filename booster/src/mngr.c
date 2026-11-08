/**
 * File: mngr.c
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2023-25 - GOODDATA LABS SL
 * Description: C file with the main loop of the manager module
 */

#include "mngr.h"

static firmware_upgrade_state_t firmware_upgrade_state = FIRMWARE_UPGRADE_IDLE;
// True while the optional upgrade.md5 companion file is being fetched, false
// once the real image download is in flight. See mngr_firmwareUpgradeStart().
static bool fw_fetching_md5 = false;
static bool network_scan_enabled = false;
static bool device_reset = false;
static bool factory_reset = false;
static absolute_time_t reboot_time = {0};
static absolute_time_t factory_reset_time = {0};

#define MNGR_OFFLINE_STATUS_MESSAGE "Offline mode. Network disabled."

void mngr_enable_network_scan() { network_scan_enabled = true; }

void mngr_disable_network_scan() { network_scan_enabled = false; }

wifi_scan_data_t *mngr_get_networks() { return network_getFoundNetworks(); }

static void mngr_enter_offline_mode(uint8_t wifi_status, const char *details,
                                    const char *mac_str) {
  const char *status_details =
      (details != NULL) ? details : MNGR_OFFLINE_STATUS_MESSAGE;

  DPRINTF("Entering manager offline mode: %s\n", status_details);
  mngr_disable_network_scan();
  network_deInit();
  display_mngr_wifi_change_status(
      wifi_status, NULL, NULL, status_details,
      (mac_str != NULL && mac_str[0] != '\0') ? mac_str : NULL);
  display_refresh();
}

int mngr_init() {
  // Initialize the network
  int err = network_initChipOnly();
  if (err != 0) {
    DPRINTF("Error initializing the network: %i\n", err);
  }

  // Initialize the SD card
  FATFS fs;
  SettingsConfigEntry *appsFolder =
      settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER);
  char *appsFolderName = "/apps";
  if (appsFolder == NULL) {
    DPRINTF(
        "APPS_FOLDER not found in the configuration. Using default value\n");
  } else {
    DPRINTF("APPS_FOLDER: %s\n", appsFolder->value);
    appsFolderName = appsFolder->value;
  }
  int sdcard_err = sdcard_initFilesystem(&fs, appsFolderName);
  if (sdcard_err != SDCARD_INIT_OK) {
    DPRINTF("Error initializing the SD card: %i\n", sdcard_err);
  } else {
    DPRINTF("SD card found & initialized\n");
  }
  appmngr_get_sdcard_info()->ready = (sdcard_err == SDCARD_INIT_OK);

  // Obtain the free space in the SD card
  uint32_t total_size = 0;
  uint32_t free_space = 0;
  sdcard_getInfo(&fs, &total_size, &free_space);
  appmngr_get_sdcard_info()->total_size = total_size;
  appmngr_get_sdcard_info()->free_space = free_space;
  DPRINTF("SD card total size: %uMB\n", appmngr_get_sdcard_info()->total_size);
  DPRINTF("SD card free space: %uMB\n", appmngr_get_sdcard_info()->free_space);
  char *apps_folder =
      settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value;
  if (apps_folder == NULL) {
    appmngr_get_sdcard_info()->apps_folder_found = false;
    DPRINTF("Apps folder param not found\n");
  } else {
    appmngr_get_sdcard_info()->apps_folder_found = sdcard_dirExist(apps_folder);
    DPRINTF("Apps folder found: %s\n",
            appmngr_get_sdcard_info()->apps_folder_found ? "true" : "false");
  }

  // Apply /.wificonf when the card carries one (EPIC-10). This runs before
  // WiFi starts, so the values take effect on this boot without a reboot,
  // and flash is written only when something changed, so a card that keeps
  // the file does not cost a flash write per boot.
  if (appmngr_get_sdcard_info()->ready) {
    wificonf_t wificonf;
    if (wificonf_readFile(&wificonf)) {
      if (wificonf_applySettings(&wificonf)) {
        DPRINTF("Settings changed by %s. Saving\n", WIFICONF_FILE);
        settings_save(gconfig_getContext(), true);
      } else {
        DPRINTF("%s matches the stored settings\n", WIFICONF_FILE);
      }
    }
  }

  // Deinit the network
  DPRINTF("Deinitializing the network\n");
  network_deInit();

  // Set hostname
  char *hostname =
      settings_find_entry(gconfig_getContext(), PARAM_HOSTNAME)->value;
  char url_host[128] = {0};
  if ((hostname != NULL) && (strlen(hostname) > 0)) {
    snprintf(url_host, sizeof(url_host), "http://%s.local", hostname);
  } else {
    snprintf(url_host, sizeof(url_host), "http://%s.local", "sidecart");
  }

  char url_ip[128] = {0};
  snprintf(url_ip, sizeof(url_ip), "http://%s", "127.0.0.1");

  // Set SSID
  char ssid[128] = {0};
  SettingsConfigEntry *ssid_param =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_SSID);
  if (strlen(ssid_param->value) == 0) {
    DPRINTF("No SSID found in config.\n");
    snprintf(ssid, sizeof(ssid), "%s", "No SSID found");
  } else {
    snprintf(ssid, sizeof(ssid), "%s", ssid_param->value);
  }

  display_mngr_start(ssid, url_host, url_ip);

  wifi_mode_t wifi_mode_value = WIFI_MODE_STA;
  bool network_features_enabled = false;
  char mac_str[64] = {0};
  err = network_wifiInit(wifi_mode_value);
  if (err != 0) {
    DPRINTF("Error initializing the network: %i\n", err);
    mngr_enter_offline_mode(DISPLAY_MNGR_WIFI_STATUS_RETRY_ERROR,
                            MNGR_OFFLINE_STATUS_MESSAGE, NULL);
  } else {
    const char *network_mac = network_getCyw43MacStr();
    snprintf(mac_str, sizeof(mac_str), "%s", network_mac ? network_mac : "");
    DPRINTF("MAC address: %s\n", mac_str);
    display_mngr_wifi_change_status(DISPLAY_MNGR_WIFI_STATUS_CONNECTING, NULL,
                                    NULL, NULL,
                                    mac_str[0] != '\0' ? mac_str : NULL);
    display_refresh();

    // Connect to the WiFi network
    int numRetries = 3;
    err = NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;
    while (err != NETWORK_WIFI_STA_CONN_OK) {
      err = network_wifiStaConnect();
      if (err < 0) {
        DPRINTF("Error connecting to the WiFi network: %i\n", err);
        DPRINTF("Number of retries left: %i\n", numRetries);
        if (--numRetries <= 0) {
          DPRINTF("Max retries reached. Switching to offline mode.\n");
          mngr_enter_offline_mode(DISPLAY_MNGR_WIFI_STATUS_OFFLINE,
                                  MNGR_OFFLINE_STATUS_MESSAGE,
                                  mac_str[0] != '\0' ? mac_str : NULL);
          break;
        }

        display_mngr_wifi_change_status(
            DISPLAY_MNGR_WIFI_STATUS_RETRY_ERROR, NULL, NULL,
            network_WifiStaConnStatusString(err),
            mac_str[0] != '\0' ? mac_str : NULL);
        display_refresh();
        sleep_ms(3000);  // Wait before retrying
        display_mngr_wifi_change_status(DISPLAY_MNGR_WIFI_STATUS_CONNECTING,
                                        NULL, NULL, NULL,
                                        mac_str[0] != '\0' ? mac_str : NULL);
        display_refresh();
      }
    }

    if (err == NETWORK_WIFI_STA_CONN_OK) {
      network_features_enabled = true;
      DPRINTF("WiFi connected\n");

      ip4_addr_t ip = network_getCurrentIp();
      DPRINTF("IP address: %s\n", ip4addr_ntoa(&ip));

      snprintf(url_ip, sizeof(url_ip), "http://%s", ip4addr_ntoa(&ip));
      version_loop();
      download_version_t version_status = version_get_status();
      if (version_status == DOWNLOAD_VERSION_COMPLETED) {
        DPRINTF("Version download completed successfully\n");
        if (version_isNewer()) {
          DPRINTF("Downloaded version is newer than the built-in version\n");
        } else {
          DPRINTF(
              "Downloaded version is not newer than the built-in version\n");
        }
      } else {
        DPRINTF("Version download failed or in progress\n");
      }

      display_mngr_wifi_change_status(DISPLAY_MNGR_WIFI_STATUS_CONNECTED,
                                      url_host, url_ip, NULL,
                                      mac_str[0] != '\0' ? mac_str : NULL);
      display_refresh();
    }
  }

  // Init the download apps
  appmngr_init();

  // Configure the sd card, if possible
  bool sdcard_ready = false;
  if (!appmngr_get_sdcard_info()->ready) {
    // Try to initialize the SD card
    SettingsConfigEntry *appsFolder =
        settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER);
    char *appsFolderName = "/apps";
    if (appsFolder == NULL) {
      DPRINTF(
          "APPS_FOLDER not found in the configuration. Using default "
          "value\n");
    } else {
      DPRINTF("APPS_FOLDER: %s\n", appsFolder->value);
      appsFolderName = appsFolder->value;
    }
    int sdcard_err = sdcard_initFilesystem(&fs, appsFolderName);
    if (sdcard_err == SDCARD_INIT_OK) {
      DPRINTF("SD card found & initialized\n");
      appmngr_get_sdcard_info()->ready = true;
      // Obtain the free space in the SD card
      uint32_t total_size = 0;
      uint32_t free_space = 0;
      sdcard_getInfo(&fs, &total_size, &free_space);
      appmngr_get_sdcard_info()->total_size = total_size;
      appmngr_get_sdcard_info()->free_space = free_space;
      DPRINTF("SD card total size: %uMB\n",
              appmngr_get_sdcard_info()->total_size);
      DPRINTF("SD card free space: %uMB\n",
              appmngr_get_sdcard_info()->free_space);
      char *apps_folder =
          settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value;
      if (apps_folder != NULL) {
        appmngr_get_sdcard_info()->apps_folder_found =
            sdcard_dirExist(apps_folder);
      }
      sdcard_ready = true;

      // We are running, so whatever the last upgrade wrote to flash boots and
      // the image it was flashed from is dead weight. An interrupted flash
      // never reaches this point: BOOT_FEATURE is still UPGRADER, so the
      // device reboots into the upgrader and retries from the same file.
      appmngr_cleanup_upgrade_image();

      // appmngr load table
      uint8_t *table = malloc(FLASH_SECTOR_SIZE);
      if (table == NULL) {
        // malloc returns NULL rather than panicking (PICO_MALLOC_PANIC 0),
        // so every allocation has to be checked. Skipping the sync leaves the
        // lookup table as it is on flash, which is recoverable; the device
        // still boots.
        DPRINTF("Cannot allocate the apps lookup table; skipping the sync\n");
      } else {
        uint16_t table_length = 0;
        appmngr_load_apps_lookup_table(table, &table_length);
        appmngr_print_apps_lookup_table(table, table_length);

        // Now, try to sync the apps lookup table with the JSON files
        appmngr_sync_lookup_table();

        // appmngr load table
        memset(table, 0, FLASH_SECTOR_SIZE);
        appmngr_load_apps_lookup_table(table, &table_length);
        appmngr_print_apps_lookup_table(table, table_length);
        free(table);
      }
    } else {
      DPRINTF("Error initializing the SD card: %i\n", sdcard_err);
    }
  }

  if (network_features_enabled) {
    // Start the HTTP server
    mngr_httpd_start();
    sleep_ms(500);
  }

  // appmngr_download_firmware_status(DOWNLOAD_STATUS_REQUESTED);

  int wifi_scan_polling_interval = 5;  // 5 seconds
  absolute_time_t wifi_scan_time =
      make_timeout_time_ms(5 * 1000);  // 3 seconds minimum for network scanning
  absolute_time_t show_screen_timeout =
      make_timeout_time_ms(10 * 1000);  // 10 seconds to show the screen
  absolute_time_t start_download_time =
      make_timeout_time_ms(86400 * 1000);  // 3 seconds to start the download
  absolute_time_t wifi_signal_refresh_time =
      make_timeout_time_ms(2 * 1000);  // 2 seconds to refresh RSSI on screen
  reboot_time = make_timeout_time_ms(0);
  factory_reset_time = make_timeout_time_ms(0);
  absolute_time_t launch_time = make_timeout_time_ms(0);
  bool bypass = true;
  while (1) {
    int wait_ms = 10;
    if (appmngr_get_download_status() == DOWNLOAD_STATUS_IN_PROGRESS ||
        appmngr_get_download_firmware_status() == DOWNLOAD_STATUS_IN_PROGRESS ||
        appmngr_get_launch_status() == DOWNLOAD_LAUNCHAPP_INPROGRESS ||
        appmngr_get_launch_status() == DOWNLOAD_LAUNCHAPP_SCHEDULED) {
      wait_ms = 1;
    }
#if PICO_CYW43_ARCH_POLL
    if (network_features_enabled) {
      network_safe_poll();
      cyw43_arch_wait_for_work_until(make_timeout_time_ms(wait_ms));
    } else {
      sleep_ms(wait_ms);
    }
#else
    sleep_ms(wait_ms);
#endif
    if (network_features_enabled && network_scan_enabled) {
      network_scan(&wifi_scan_time, wifi_scan_polling_interval);
    }
    if (network_features_enabled && !term_isActive() &&
        absolute_time_diff_us(get_absolute_time(), wifi_signal_refresh_time) <
            0) {
      display_mngr_refresh_connection_info();
      wifi_signal_refresh_time = make_timeout_time_ms(2 * 1000);
    }
    if (bypass &&
        (absolute_time_diff_us(get_absolute_time(), show_screen_timeout) < 0)) {
      // Send continue to desktop command
      // SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_CONTINUE);
      bypass = false;
    }

    if (network_features_enabled &&
        (appmngr_get_download_status() == DOWNLOAD_STATUS_IN_PROGRESS ||
         appmngr_get_download_firmware_status() ==
             DOWNLOAD_STATUS_IN_PROGRESS)) {
      appmngr_poll_download_app();
    }

    if (network_features_enabled) {
      switch (appmngr_get_download_status()) {
        case DOWNLOAD_STATUS_REQUESTED: {
          start_download_time = make_timeout_time_ms(
              3 * 1000);  // 3 seconds to start the download
          appmngr_set_download_phase(APPMNGR_PHASE_CONNECT);
          appmngr_download_status(DOWNLOAD_STATUS_NOT_STARTED);
          break;
        }
        case DOWNLOAD_STATUS_NOT_STARTED: {
          if ((absolute_time_diff_us(get_absolute_time(), start_download_time) <
               0)) {
            // Start the download. NULL means use info in app_info
            err = appmngr_start_download(NULL);
            if (err != DOWNLOAD_OK) {
              DPRINTF("Error downloading app. Drive to error page.\n");
            }
          }
          break;
        }
        case DOWNLOAD_STATUS_STARTED: {
          if (appmngr_get_download_error() != DOWNLOAD_OK) {
            DPRINTF("Error downloading app. Drive to error page.\n");
            appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
            appmngr_confirm_failed_download_app();
            appmngr_download_status(DOWNLOAD_STATUS_FAILED);
          }
          break;
        }
        case DOWNLOAD_STATUS_COMPLETED: {
          // The transfer is done; what follows is the MD5 read and the flash
          // work. appmngr_finish_download_app() advances the phase to INSTALL
          // itself once the hash matches.
          appmngr_set_download_phase(APPMNGR_PHASE_VERIFY);
          // Save the app info to the SD card
          download_err_t err = appmngr_finish_download_app();
          appmngr_download_error(err);
          if (err != DOWNLOAD_OK) {
            DPRINTF("Error finishing download app\n");
            appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
            appmngr_confirm_failed_download_app();
            appmngr_download_status(DOWNLOAD_STATUS_FAILED);
            break;
          }
          appmngr_confirm_download_app();
          appmngr_set_download_phase(APPMNGR_PHASE_DONE);
          appmngr_download_status(DOWNLOAD_STATUS_IDLE);
          break;
        }
      }
    }

    if (network_features_enabled) {
      switch (appmngr_get_download_firmware_status()) {
        case DOWNLOAD_STATUS_REQUESTED: {
          start_download_time = make_timeout_time_ms(
              3 * 1000);  // 3 seconds to start the download
          appmngr_set_download_phase(APPMNGR_PHASE_CONNECT);
          appmngr_download_firmware_status(DOWNLOAD_STATUS_NOT_STARTED);
          break;
        }
        case DOWNLOAD_STATUS_NOT_STARTED: {
          if ((absolute_time_diff_us(get_absolute_time(), start_download_time) <
               0)) {
            err = appmngr_start_download(
                fw_fetching_md5 ? FIRMWARE_MD5_URL : FIRMWARE_BINARY_URL);
            if (err != DOWNLOAD_OK) {
              DPRINTF("Error downloading firmware. Drive to error page.\n");
            }
          }
          break;
        }
        case DOWNLOAD_STATUS_FAILED: {
          if (fw_fetching_md5) {
            // The checksum is required. Without it there is no way to tell a
            // good image from a truncated or corrupted one, so refuse rather
            // than flash something unverified.
            DPRINTF("upgrade.md5 unavailable; refusing the upgrade\n");
            fw_fetching_md5 = false;
            appmngr_clear_expected_firmware_md5();
            appmngr_set_progress_reporting(true);
            appmngr_download_firmware_error(DOWNLOAD_MD5UNAVAILABLE_ERROR);
            appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
            firmware_upgrade_state = FIRMWARE_UPGRADE_FAILED;
          }
          break;
        }
        case DOWNLOAD_STATUS_STARTED: {
          if (appmngr_get_download_firmware_error() != DOWNLOAD_OK) {
            DPRINTF("Error downloading firmware. Drive to error page.\n");
            appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
            appmngr_confirm_failed_download_firmware();
            appmngr_download_firmware_status(DOWNLOAD_STATUS_FAILED);
          } else {
            display_mngr_change_status(5, NULL);  // Downloading firmware message
            display_refresh();
          }

          break;
        }
        case DOWNLOAD_STATUS_COMPLETED: {
          if (fw_fetching_md5) {
            // Stage 1 done. A file that arrives but cannot be parsed is as
            // fatal as one that never arrived.
            bool got_digest = appmngr_load_expected_firmware_md5_from_tmp();
            fw_fetching_md5 = false;
            appmngr_set_progress_reporting(true);
            if (!got_digest) {
              DPRINTF("upgrade.md5 malformed; refusing the upgrade\n");
              appmngr_download_firmware_error(DOWNLOAD_MD5UNAVAILABLE_ERROR);
              appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
              appmngr_download_firmware_status(DOWNLOAD_STATUS_FAILED);
              firmware_upgrade_state = FIRMWARE_UPGRADE_FAILED;
              break;
            }
            appmngr_download_firmware_error(DOWNLOAD_OK);
            appmngr_set_download_phase(APPMNGR_PHASE_CONNECT);
            appmngr_download_firmware_status(DOWNLOAD_STATUS_REQUESTED);
            break;
          }

          // Stage 2 done: verify before the image goes anywhere near flash.
          // With no published digest this returns OK and changes nothing.
          firmware_upgrade_state = FIRMWARE_UPGRADE_VERIFYING;
          appmngr_set_download_phase(APPMNGR_PHASE_VERIFY);
          download_err_t md5_err = appmngr_verify_firmware_md5();
          if (md5_err != DOWNLOAD_OK) {
            DPRINTF("Firmware image failed verification; aborting\n");
            appmngr_download_firmware_error(md5_err);
            appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
            appmngr_confirm_failed_download_firmware();
            appmngr_download_firmware_status(DOWNLOAD_STATUS_FAILED);
            firmware_upgrade_state = FIRMWARE_UPGRADE_FAILED;
            break;
          }

          // Promote the verified image to upgrade.bin NOW, not when the user
          // confirms. tmp.download is shared scratch: the microfirmware
          // installs, this download and the upgrade.md5 fetch all write it.
          // Renaming at confirm time meant anything downloaded in between --
          // a microfirmware install, say -- was promoted instead, so the
          // upgrader could write a microfirmware into the booster slot. The
          // image is verified and named in one step here, and upgrade.bin is
          // written by this path only.
          download_err_t close_err = appmngr_finish_download_firmware();
          if (close_err != DOWNLOAD_OK) {
            DPRINTF("Error closing the firmware image: %d\n", close_err);
            appmngr_download_firmware_error(close_err);
            appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
            appmngr_confirm_failed_download_firmware();
            appmngr_download_firmware_status(DOWNLOAD_STATUS_FAILED);
            firmware_upgrade_state = FIRMWARE_UPGRADE_FAILED;
            break;
          }
          download_err_t promote_err = appmngr_confirm_download_firmware();
          if (promote_err != DOWNLOAD_OK) {
            DPRINTF("Error promoting the image to upgrade.bin: %d\n",
                    promote_err);
            appmngr_download_firmware_error(promote_err);
            appmngr_set_download_phase(APPMNGR_PHASE_FAILED);
            appmngr_confirm_failed_download_firmware();
            appmngr_download_firmware_status(DOWNLOAD_STATUS_FAILED);
            firmware_upgrade_state = FIRMWARE_UPGRADE_FAILED;
            break;
          }

          // Save the app info to the SD card
          appmngr_download_firmware_error(DOWNLOAD_OK);
          appmngr_set_download_phase(APPMNGR_PHASE_DONE);
          appmngr_download_firmware_status(DOWNLOAD_STATUS_IDLE);
          firmware_upgrade_state = FIRMWARE_UPGRADE_DOWNLOADED;
          display_mngr_change_status(6, NULL);  // Upgrading firmware message
          display_refresh();
          break;
        }
      }
    }

    if (appmngr_get_launch_status() == DOWNLOAD_LAUNCHAPP_INPROGRESS) {
      if ((absolute_time_diff_us(get_absolute_time(), launch_time) < 0)) {
        download_launch_err_t err = appmngr_launch_app();
        if (err == DOWNLOAD_LAUNCHAPP_OK) {
          device_reset = true;
          reboot_time = make_timeout_time_ms(0);
        } else {
          // Leave the in-progress state, or the loop retries the launch on
          // every pass. The status keeps the error for whoever asks.
          appmngr_set_launch_status(err);
          display_mngr_change_status(
              7, err == DOWNLOAD_LAUNCHAPP_NOCONFIGSECTOR_ERROR
                     ? "Cannot launch: no settings slot"
                     : "Cannot launch the app");
          display_refresh();
        }
      }
    } else if (appmngr_get_launch_status() == DOWNLOAD_LAUNCHAPP_SCHEDULED) {
      appmngr_set_launch_status(DOWNLOAD_LAUNCHAPP_INPROGRESS);
      display_mngr_change_status(4, NULL);  // Launching app message
      display_refresh();
      launch_time = make_timeout_time_ms(5 * 1000);  // 5 seconds to launch
      DPRINTF("Launching app in 5 seconds\n");
    }

    // Check if the device needs to be reset
    if (device_reset) {
      if (absolute_time_diff_us(get_absolute_time(), reboot_time) < 0) {
        DPRINTF("Rebooting\n");
        device_reset = false;
        // Send the reboot command
        SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
        sleep_ms(1000);
        reset_device();
      }
    }

    // Check if the device needs to be factory reset
    if (factory_reset) {
      if (absolute_time_diff_us(get_absolute_time(), factory_reset_time) < 0) {
        DPRINTF("Factory reset\n");
        factory_reset = false;
        // Check if there is a file named .notreboot
        // in the root of the SD card. If so, do not erase the flash
        // Only the presence of the .notreboot file is checked; its contents are
        // ignored.
        FILINFO fno;
        FRESULT res = f_stat("/.notreboot", &fno);
        if (res == FR_OK) {
          DPRINTF(".notreboot file found. Not erasing flash.\n");
          display_mngr_change_status(7, NULL);  // Erase but not reboot message
          display_refresh();
          reset_eraseFlash();
          while (true) {
            sleep_ms(30000);
          }
        } else {
          // Send the reboot command
          SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
          sleep_ms(1000);
          reset_deviceAndEraseFlash();
        }
      }
    }

    // Check remote commands
    term_loop();
  }
}

void mngr_schedule_reset(int seconds) {
  DPRINTF("Rebooting in %d seconds\n", seconds);
  device_reset = true;
  reboot_time = make_timeout_time_ms(seconds * 1000);
}

void mngr_schedule_factory_reset(int seconds) {
  DPRINTF("Factory reset in %d seconds\n", seconds);
  factory_reset = true;
  factory_reset_time = make_timeout_time_ms(seconds * 1000);
}

void mngr_loop() {}

/**
 * @brief Begin the firmware upgrade download process.
 *
 * Sets the firmware download state machine to REQUESTED so the main loop
 * transitions into starting the download of the firmware binary URL.
 * Also clears any previous firmware download error.
 */
void mngr_firmwareUpgradeStart(void) {
  DPRINTF("mngr_firmwareUpgradeStart: fetching upgrade.md5 first\n");
  firmware_upgrade_state = FIRMWARE_UPGRADE_DOWNLOADING;
  // The digest is fetched BEFORE the image because both downloads land in the
  // same temp file; the other order would overwrite the image we just fetched.
  // It is optional, so a 404 must not cost three retry attempts.
  fw_fetching_md5 = true;
  appmngr_clear_expected_firmware_md5();
  // Normal retry budget: the checksum is mandatory, so a dropped connection
  // must not be mistaken for "not published" and fail the upgrade.
  appmngr_set_progress_reporting(false);
  appmngr_download_firmware_error(DOWNLOAD_OK);
  appmngr_download_firmware_status(DOWNLOAD_STATUS_REQUESTED);
}

/**
 * @brief Clean/reset any firmware upgrade transient state.
 *
 * Returns the firmware download state machine to IDLE and clears any stored
 * error, effectively aborting or resetting the flow.
 */
void mngr_firmwareUpgradeClean(void) {
  DPRINTF("mngr_firmwareUpgradeClean: resetting firmware upgrade state\n");
  firmware_upgrade_state = FIRMWARE_UPGRADE_IDLE;
  fw_fetching_md5 = false;
  appmngr_clear_expected_firmware_md5();
  appmngr_set_progress_reporting(true);
  appmngr_download_firmware_error(DOWNLOAD_OK);
  appmngr_download_firmware_status(DOWNLOAD_STATUS_IDLE);
}

void mngr_firmwareUpgradeInstall(void) {
  DPRINTF("mngr_firmwareUpgradeInstall: installing firmware\n");
  firmware_upgrade_state = FIRMWARE_UPGRADE_INSTALLING;
  appmngr_download_firmware_error(DOWNLOAD_OK);
  appmngr_download_firmware_status(DOWNLOAD_STATUS_IDLE);

  // The image was closed, verified and renamed to upgrade.bin when the
  // download finished, so nothing is left to do here but write the upgrader
  // to the start of flash and reboot into it. This function does not return.
  appmngr_firmwareUpgradeStart();
}

firmware_upgrade_state_t mngr_get_firmwareUpgradeState(void) {
  return firmware_upgrade_state;
}
