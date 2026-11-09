/**
 * File: main.c
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that launches the ROM emulator
 */

#include "fabric.h"
#include "select.h"

static int wait_for_reboot = 2;
static int wait_poll_interval_ms = 1000;
static fabric_config_t fabric_config = {0};
static bool page_served = false;

// Store the mode and the boot feature that put Booster into Manager mode on
// the next boot, persist everything, and reboot. Shared by the two ways of
// getting WiFi credentials in factory mode: the web form and .wificonf.
static void fabric_saveAndReboot(void) {
  settings_put_integer(gconfig_getContext(), PARAM_WIFI_MODE, WIFI_MODE_STA);
  settings_put_string(gconfig_getContext(), PARAM_BOOT_FEATURE, "BOOSTER");
  settings_save(gconfig_getContext(), true);
  DPRINTF("STA parameters save. Reboot!\n");

  multicore_lockout_victim_init();  // keep the core 1 locked out

  // Send the reboot command
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
  reset_device();
}

static void saveWifiParams() {
  // Save the parameters
  DPRINTF("Store all the parameters needed to start the BOOSTER in STA mode\n");
  settings_put_string(gconfig_getContext(), PARAM_WIFI_SSID,
                      fabric_config.ssid);
  settings_put_string(gconfig_getContext(), PARAM_WIFI_PASSWORD,
                      fabric_config.pass);
  settings_put_integer(gconfig_getContext(), PARAM_WIFI_AUTH,
                       fabric_config.auth);
  fabric_saveAndReboot();
}

/**
 * @brief Sets the configuration for the fabric network.
 *
 * This function sets the SSID, password, authentication type, and status
 * of the fabric network configuration.
 *
 * @param ssid The SSID of the network. Must be a null-terminated string with a
 * maximum length of MAX_SSID_LENGTH.
 * @param pass The password of the network. Must be a null-terminated string
 * with a maximum length of MAX_PASSWORD_LENGTH.
 * @param auth The authentication type for the network.
 * @param is_set A boolean indicating whether the configuration is set.
 */
void fabric_set_config(char ssid[MAX_SSID_LENGTH],
                       char pass[MAX_PASSWORD_LENGTH], uint16_t auth,
                       bool is_set) {
  strncpy(fabric_config.ssid, ssid, MAX_SSID_LENGTH - 1);
  fabric_config.ssid[MAX_SSID_LENGTH - 1] = '\0';  // Ensure null-termination

  strncpy(fabric_config.pass, pass, MAX_PASSWORD_LENGTH - 1);
  fabric_config.pass[MAX_PASSWORD_LENGTH - 1] =
      '\0';  // Ensure null-termination

  fabric_config.auth = auth;

  fabric_config.is_set = is_set;
}

void fabric_served_callback() {
  DPRINTF("Serving page\n");
  if (!page_served) {
    display_fabric_portal_change_status(2);
    display_refresh();
    page_served = true;
  }
}

/**
 * @brief Initializes the fabric network and starts the HTTP server.
 *
 * This function sets up the WiFi in Access Point (AP) mode and initializes the
 * network. If the network initialization fails, it logs an error message,
 * blinks an error indicator, and returns the error code. If successful, it logs
 * the WiFi mode, turns on a blink indicator, and starts the HTTP server.
 *
 * @return int Returns 0 on success, or an error code on failure.
 */
int fabric_init() {
  // LEt's clean the flash memory starting in the
  unsigned int config_flash_length = (unsigned int)&_global_lookup_flash_start -
                                     (unsigned int)&_config_flash_start;
  unsigned int global_lookup_flash_length = FLASH_SECTOR_SIZE;
  unsigned int global_config_flash_length = FLASH_SECTOR_SIZE;
  unsigned int total_config_flash_length = config_flash_length +
                                           global_lookup_flash_length +
                                           global_config_flash_length;
  DPRINTF(
      "Erasing configuration and lookup tables starting at 0x%08X for %d "
      "bytes\n",
      (unsigned int)&_config_flash_start, total_config_flash_length);
  // Erase the configuration and lookup tables previously used
  select_flashLockoutBegin();
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase((unsigned int)&_config_flash_start - XIP_BASE,
                    total_config_flash_length);
  restore_interrupts(ints);
  select_flashLockoutEnd();
  DPRINTF("Configuration and lookup tables erased\n");

  // First, check if there is a Wifi configuration file in the microSD card.
  // Initialize the SD card
  FATFS fs;
  int sdcard_err = sdcard_initFilesystem(&fs, "/");
  bool scardInitialized = (sdcard_err == SDCARD_INIT_OK);
  if (sdcard_err != SDCARD_INIT_OK) {
    DPRINTF("Error initializing the SD card: %i\n", sdcard_err);
  } else {
    DPRINTF("SD card found & initialized\n");
  }

  if (scardInitialized) {
    // The .wificonf file, when present, is the configuration. It
    // goes straight into the settings; Manager mode reads the same file on
    // its own boots, see mngr_init().
    wificonf_t wificonf;
    if (wificonf_readFile(&wificonf)) {
      wificonf_applySettings(&wificonf);
      fabric_config.is_set = true;
      DPRINTF("WiFi configuration loaded from SD card\n");
    } else {
      DPRINTF("No WiFi configuration file found on SD card\n");
    }
  }

  if (!fabric_config.is_set) {
    wifi_mode_t wifi_mode_value = WIFI_MODE_AP;
    int err = network_wifiInit(wifi_mode_value);
    if (err != 0) {
      DPRINTF("Error initializing the network: %i\n", err);
      blink_error();
      return err;
    }

    DPRINTF("WiFi mode is AP\n");
    blink_on();

    // Start the HTTP server
    fabric_httpd_start((fabric_httpd_callback_t)fabric_set_config,
                       (fabric_httpd_served_callback_t)fabric_served_callback);
  } else {
    DPRINTF("Network configuration found in %s. Saving and rebooting\n",
            WIFICONF_FILE);
    fabric_saveAndReboot();
  }
  return 0;
}

void fabric_httpd_get_ip(dhcp_server_t *d, dhcp_msg_t *msg, struct netif *nif) {
  if (msg->yiaddr[0] == 0 && msg->yiaddr[1] == 0 && msg->yiaddr[2] == 0 &&
      msg->yiaddr[3] == 0) {
    DPRINTF("No IP address assigned\n");
    return;
  }
  DPRINTF("Got IP: %d.%d.%d.%d\n", msg->yiaddr[0], msg->yiaddr[1],
          msg->yiaddr[2], msg->yiaddr[3]);
  display_fabric_wifi_change_status(2);
  display_fabric_portal_change_status(1);
  display_fabric_portal_connection();
}

/**
 * @brief Main loop for fabric operations.
 *
 * This function performs the main loop for fabric operations, including network
 * scanning and handling reboot requests. It runs until the fabric configuration
 * is set, and then waits for a reboot signal.
 *
 * @note The function uses different mechanisms for polling and waiting based on
 * the PICO_CYW43_ARCH_POLL macro.
 */
void fabric_loop() {
  char url_gw[128] = {0};
  snprintf(url_gw, sizeof(url_gw), "http://%s", WIFI_AP_GATEWAY);
  char url_host[128] = {0};
  snprintf(url_host, sizeof(url_host), "http://%s.local", WIFI_AP_HOSTNAME);
  set_dhcp_server_cb(fabric_httpd_get_ip);
  const char *authType = network_getAuthTypeStringShort(WIFI_AP_AUTH);
  display_fabric_start(WIFI_AP_SSID, WIFI_AP_PASS, authType, url_gw, url_host);

  display_fabric_wifi_change_status(1);
  display_refresh();
  int wifi_scan_polling_interval = 5;  // 5 seconds
  absolute_time_t wifi_scan_time =
      make_timeout_time_ms(5 * 1000);  // 3 seconds minimum for network scanning
  while (!fabric_config.is_set) {
#if PICO_CYW43_ARCH_POLL
    network_safe_poll();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
#else
    sleep_ms(10);
#endif
    // printf("Time elapsed: %lld microseconds\n", wifi_scan_time);
    // blink_morse('A');
    network_scan(&wifi_scan_time, wifi_scan_polling_interval);
    // Set the flag to NOT-RESET the computer
  }
  DPRINTF("Wifi SSI: %s, Pass:%s, Auth:%i\n", fabric_config.ssid,
          fabric_config.pass, fabric_config.auth);

  // We must do this to avoid failures in the network deinit
  while (network_scanIsActive()) {
#if PICO_CYW43_ARCH_POLL
    network_safe_poll();
    cyw43_arch_wait_for_work_until(
        make_timeout_time_ms(wait_poll_interval_ms));
#else
    sleep_ms(wait_poll_interval_ms);
#endif
  }

  display_fabric_reset();

  // We have to give a few seconds to render the last page
  int wait_for_reboot = 1;
  while (wait_for_reboot--) {
#if PICO_CYW43_ARCH_POLL
    network_safe_poll();
    cyw43_arch_wait_for_work_until(
        make_timeout_time_ms(wait_poll_interval_ms));
#else
    sleep_ms(wait_poll_interval_ms);
#endif
  }

  // Deinit the DHCP server
  dhcp_server_deinit();

  // Deinit the DNS server
  dns_server_deinit();

  // Leave the AP interface
  cyw43_wifi_leave(&cyw43_state, CYW43_ITF_AP);
  cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_AP, false, 0);

  // Deinit the network
  network_deInit();

  // Force hard power-off
#ifdef CYW43_WL_GPIO_WL_REG_ON
  cyw43_arch_gpio_put(CYW43_WL_GPIO_WL_REG_ON, 0);
#endif

  // Wait for the page before rebooting
  sleep_ms(wait_poll_interval_ms);
  saveWifiParams();
}
