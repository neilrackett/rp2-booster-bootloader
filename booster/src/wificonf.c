/**
 * File: wificonf.c
 * Author: Diego Parrilla Santamaría
 * Date: September 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The .wificonf file on the microSD card (EPIC-10, D-09)
 */

#include "wificonf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ff.h"
#include "gconfig.h"

bool wificonf_isPresent(void) {
  FILINFO info = {0};
  return f_stat(WIFICONF_FILE, &info) == FR_OK;
}

bool wificonf_readFile(wificonf_t *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  FIL file;
  FRESULT res = f_open(&file, WIFICONF_FILE, FA_READ);
  if (res != FR_OK) {
    DPRINTF("No %s on the SD card (%d)\n", WIFICONF_FILE, (int)res);
    return false;
  }
  char line[WIFICONF_LINE_MAX];
  while (f_gets(line, sizeof(line), &file) != NULL) {
    wificonf_parseLine(cfg, line);
  }
  f_close(&file);
  DPRINTF(
      "%s: SSID=%s (%s) AUTH=%u (%s) %s=%s address=%s netmask=%s "
      "gateway=%s dns=%s%s\n",
      WIFICONF_FILE, cfg->ssid, cfg->hasSsid ? "set" : "absent",
      (unsigned)cfg->auth, cfg->hasAuth ? "set" : "absent", WIFICONF_KEY_DHCP,
      cfg->dhcp == WIFICONF_DHCP_UNSET ? "absent"
      : cfg->dhcp == WIFICONF_DHCP_ON  ? "on"
                                       : "off",
      cfg->address, cfg->netmask, cfg->gateway, cfg->dns,
      cfg->tcpipInvalid ? " (a TCPIP_* line was invalid)" : "");
  return true;
}

// The three writers below only touch the settings when the stored text
// differs, and say so. That is what lets a card that keeps the file boot
// without a flash write every time.
static bool wificonf_putString(const char *key, const char *value) {
  SettingsConfigEntry *entry = settings_find_entry(gconfig_getContext(), key);
  if (entry != NULL && strcmp(entry->value, value) == 0) {
    return false;
  }
  settings_put_string(gconfig_getContext(), key, value);
  return true;
}

static bool wificonf_putBool(const char *key, bool value) {
  const char *text = "false";
  if (value) {
    text = "true";
  }
  SettingsConfigEntry *entry = settings_find_entry(gconfig_getContext(), key);
  if (entry != NULL && strcmp(entry->value, text) == 0) {
    return false;
  }
  settings_put_bool(gconfig_getContext(), key, value);
  return true;
}

static bool wificonf_putInteger(const char *key, int value) {
  char text[SETTINGS_MAX_VALUE_LENGTH] = {0};
  snprintf(text, sizeof(text), "%d", value);
  SettingsConfigEntry *entry = settings_find_entry(gconfig_getContext(), key);
  if (entry != NULL && strcmp(entry->value, text) == 0) {
    return false;
  }
  settings_put_integer(gconfig_getContext(), key, value);
  return true;
}

// Accumulate "did anything change" without short-circuiting: every writer
// must run, and a plain `changed = f() || changed` is an int in C.
static void wificonf_noteChange(bool *changed, bool didChange) {
  if (didChange) {
    *changed = true;
  }
}

// Apply the TCPIP_* block (D-09). No TCPIP_DHCP line: the stored setting
// stands. TCPIP_DHCP on: stored as such, address lines ignored. TCPIP_DHCP
// off: all or nothing. Address, netmask and gateway must all have parsed and
// no TCPIP_* line may have failed, or the block is dropped and the stored
// setting stands; DNS is optional and the stored resolver stands without it.
// Nothing partial ever reaches the settings, so a wrong static configuration
// leaves the device as it was rather than dark.
static bool wificonf_applyTcpip(const wificonf_t *cfg) {
  switch (cfg->dhcp) {
    case WIFICONF_DHCP_UNSET:
      DPRINTF("No %s in %s. Stored TCP/IP settings stand\n", WIFICONF_KEY_DHCP,
              WIFICONF_FILE);
      return false;
    case WIFICONF_DHCP_ON:
      DPRINTF("DHCP enabled by %s\n", WIFICONF_FILE);
      return wificonf_putBool(PARAM_WIFI_DHCP, true);
    case WIFICONF_DHCP_OFF:
      break;
  }

  const char *missing = NULL;
  if (cfg->address[0] == '\0') {
    missing = WIFICONF_KEY_ADDRESS;
  } else if (cfg->netmask[0] == '\0') {
    missing = WIFICONF_KEY_NETMASK;
  } else if (cfg->gateway[0] == '\0') {
    missing = WIFICONF_KEY_GATEWAY;
  }
  if (missing != NULL) {
    DPRINTF(
        "Static TCP/IP dropped: %s missing or invalid. Stored settings "
        "stand\n",
        missing);
    return false;
  }
  if (cfg->tcpipInvalid) {
    DPRINTF(
        "Static TCP/IP dropped: a TCPIP_* line was invalid. Stored "
        "settings stand\n");
    return false;
  }

  bool changed = false;
  wificonf_noteChange(&changed, wificonf_putBool(PARAM_WIFI_DHCP, false));
  wificonf_noteChange(&changed,
                      wificonf_putString(PARAM_WIFI_IP, cfg->address));
  wificonf_noteChange(&changed,
                      wificonf_putString(PARAM_WIFI_NETMASK, cfg->netmask));
  wificonf_noteChange(&changed,
                      wificonf_putString(PARAM_WIFI_GATEWAY, cfg->gateway));
  if (cfg->dns[0] != '\0') {
    wificonf_noteChange(&changed, wificonf_putString(PARAM_WIFI_DNS, cfg->dns));
  }
  DPRINTF("Static TCP/IP from %s: %s mask %s gateway %s dns %s\n",
          WIFICONF_FILE, cfg->address, cfg->netmask, cfg->gateway,
          cfg->dns[0] != '\0' ? cfg->dns : "(stored)");
  return changed;
}

bool wificonf_applySettings(const wificonf_t *cfg) {
  bool changed = false;
  if (cfg->hasSsid) {
    wificonf_noteChange(&changed,
                        wificonf_putString(PARAM_WIFI_SSID, cfg->ssid));
  }
  if (cfg->hasPass) {
    wificonf_noteChange(&changed,
                        wificonf_putString(PARAM_WIFI_PASSWORD, cfg->pass));
  }
  if (cfg->hasAuth) {
    wificonf_noteChange(&changed,
                        wificonf_putInteger(PARAM_WIFI_AUTH, cfg->auth));
  }
  wificonf_noteChange(&changed, wificonf_applyTcpip(cfg));
  DPRINTF("%s applied; settings %s\n", WIFICONF_FILE,
          changed ? "changed" : "unchanged");
  return changed;
}
