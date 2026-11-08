/**
 * File: wificonf_parse.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Parse the lines of the .wificonf file on the microSD card
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "debug.h"
#include "wificonf.h"

// Trim leading and trailing blanks in place; returns the new start.
static char *wificonf_trimBlanks(char *text) {
  while (*text == ' ' || *text == '\t') {
    text++;
  }
  size_t len = strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
    text[--len] = '\0';
  }
  return text;
}

// Boolean grammar of TCPIP_DHCP: 1/True/Yes/T/Y and 0/False/No/F/N,
// case-insensitive. Anything else is WIFICONF_DHCP_UNSET, which the caller
// treats as if the line were absent. Deliberately stricter than the Manager
// form, where anything not starting with y or t counts as false: a typo in
// this file must not silently disable DHCP.
static wificonf_dhcp_t wificonf_parseBool(const char *value) {
  static const char *const onWords[] = {"1", "true", "yes", "t", "y"};
  static const char *const offWords[] = {"0", "false", "no", "f", "n"};
  for (size_t i = 0; i < sizeof(onWords) / sizeof(onWords[0]); i++) {
    if (strcasecmp(value, onWords[i]) == 0) {
      return WIFICONF_DHCP_ON;
    }
  }
  for (size_t i = 0; i < sizeof(offWords) / sizeof(offWords[0]); i++) {
    if (strcasecmp(value, offWords[i]) == 0) {
      return WIFICONF_DHCP_OFF;
    }
  }
  return WIFICONF_DHCP_UNSET;
}

// Store one validated TCPIP_* value, or record the failure. An invalid line
// empties the field and flags the whole block, so the operator gets the
// stored setting and a log line naming the key rather than half of what they
// wrote.
static void wificonf_parseTcpipValue(wificonf_t *cfg, const char *key,
                                     const char *value,
                                     bool (*parse)(const char *, char *),
                                     char *out) {
  if (parse(value, out)) {
    return;
  }
  out[0] = '\0';
  cfg->tcpipInvalid = true;
  (void)key;  // read only by the debug trace
  DPRINTF("Invalid %s in %s: '%s'\n", key, WIFICONF_FILE, value);
}

// One line, "KEY=value". Keys are matched case-insensitively; blank lines and
// lines starting with ';' or '#' are comments; unknown keys are ignored. The
// three WiFi keys receive the text after '=' untouched, exactly as the
// factory-mode parser did before v2.4.2; the TCPIP_* values are trimmed and
// validated.
void wificonf_parseLine(wificonf_t *cfg, char *line) {
  // Trim trailing newline/carriage return
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
    line[--len] = '\0';
  }

  char *start = line;
  while (*start == ' ' || *start == '\t') {
    start++;
  }
  if (*start == '\0' || *start == ';' || *start == '#') {
    return;
  }

  char *separator = strchr(start, '=');
  if (separator == NULL) {
    DPRINTF("Ignoring line without '=' in %s: %s\n", WIFICONF_FILE, start);
    return;
  }
  *separator = '\0';
  const char *key = wificonf_trimBlanks(start);
  char *value = separator + 1;

  if (strcasecmp(key, WIFICONF_KEY_SSID) == 0) {
    char raw[MAX_SSID_LENGTH * 2];
    strncpy(raw, value, sizeof(raw) - 1);
    raw[sizeof(raw) - 1] = '\0';
    if (!network_parseSSID(raw, cfg->ssid)) {
      DPRINTF("Invalid SSID in %s\n", WIFICONF_FILE);
    }
    cfg->ssid[MAX_SSID_LENGTH - 1] = '\0';
    cfg->hasSsid = true;
  } else if (strcasecmp(key, WIFICONF_KEY_PASS) == 0) {
    char raw[MAX_PASSWORD_LENGTH * 2];
    strncpy(raw, value, sizeof(raw) - 1);
    raw[sizeof(raw) - 1] = '\0';
    // An empty value is an open network, not a mistake worth a log line.
    if (!network_parsePassword(raw, cfg->pass) && raw[0] != '\0') {
      DPRINTF(
          "Invalid password in %s: a WPA2 password has %d to %d "
          "printable characters\n",
          WIFICONF_FILE, NETWORK_WPA_PASSPHRASE_MIN_LENGTH,
          NETWORK_WPA_PASSPHRASE_MAX_LENGTH);
    }
    cfg->pass[MAX_PASSWORD_LENGTH - 1] = '\0';
    cfg->hasPass = true;
  } else if (strcasecmp(key, WIFICONF_KEY_AUTH) == 0) {
    cfg->auth = (uint16_t)atoi(value);
    cfg->hasAuth = true;
  } else if (strcasecmp(key, WIFICONF_KEY_DHCP) == 0) {
    const char *word = wificonf_trimBlanks(value);
    cfg->dhcp = wificonf_parseBool(word);
    if (cfg->dhcp == WIFICONF_DHCP_UNSET) {
      DPRINTF("Invalid %s value '%s' in %s; treating the line as absent\n", key,
              word, WIFICONF_FILE);
    }
  } else if (strcasecmp(key, WIFICONF_KEY_ADDRESS) == 0) {
    wificonf_parseTcpipValue(cfg, key, wificonf_trimBlanks(value),
                             network_parseIPv4Address, cfg->address);
  } else if (strcasecmp(key, WIFICONF_KEY_NETMASK) == 0) {
    wificonf_parseTcpipValue(cfg, key, wificonf_trimBlanks(value),
                             network_parseIPv4Netmask, cfg->netmask);
  } else if (strcasecmp(key, WIFICONF_KEY_GATEWAY) == 0) {
    wificonf_parseTcpipValue(cfg, key, wificonf_trimBlanks(value),
                             network_parseIPv4Address, cfg->gateway);
  } else if (strcasecmp(key, WIFICONF_KEY_DNS) == 0) {
    wificonf_parseTcpipValue(cfg, key, wificonf_trimBlanks(value),
                             network_parseDnsList, cfg->dns);
  } else {
    DPRINTF("Ignoring unknown key in %s: %s\n", WIFICONF_FILE, key);
  }
}
