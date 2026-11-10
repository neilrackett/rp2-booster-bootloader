/**
 * File: wificonf.h
 * Author: Diego Parrilla Santamaría
 * Date: September 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The .wificonf file on the microSD card
 *
 * Read at every Booster boot, factory and Manager alike, before WiFi starts.
 * While the file is on the card it is the WiFi configuration: what it says is
 * applied to the global settings, and flash is written only when a value
 * actually changed. Remove the file to manage WiFi from the web UI again.
 */

#ifndef WIFICONF_H
#define WIFICONF_H

#include <stdbool.h>
#include <stdint.h>

#include "network_parse.h"

#define WIFICONF_FILE "/.wificonf"
#define WIFICONF_LINE_MAX 128

// Keys, matched case-insensitively against the text before the first '='.
// The three WiFi keys came first; the TCPIP_* block is optional and follows
// the rules documented on the apply function below.
#define WIFICONF_KEY_SSID "SSID"
#define WIFICONF_KEY_PASS "PASS"
#define WIFICONF_KEY_AUTH "AUTH"
#define WIFICONF_KEY_DHCP "TCPIP_DHCP"
#define WIFICONF_KEY_ADDRESS "TCPIP_ADDRESS"
#define WIFICONF_KEY_NETMASK "TCPIP_NETMASK"
#define WIFICONF_KEY_GATEWAY "TCPIP_GATEWAY"
#define WIFICONF_KEY_DNS "TCPIP_DNS"

typedef enum {
  WIFICONF_DHCP_UNSET = 0,  // no TCPIP_DHCP line: the stored setting stands
  WIFICONF_DHCP_ON,
  WIFICONF_DHCP_OFF
} wificonf_dhcp_t;

typedef struct {
  // WiFi block. has* records that the key was present; the value is what
  // network_parseSSID() / network_parsePassword() made of the text, exactly
  // as the factory-mode parser originally stored it.
  bool hasSsid;
  char ssid[MAX_SSID_LENGTH];
  bool hasPass;
  char pass[MAX_PASSWORD_LENGTH];
  bool hasAuth;
  uint16_t auth;
  // TCPIP_* block. The strings hold canonical dotted-quad text and are empty
  // when the key was absent or invalid. tcpipInvalid records that some
  // TCPIP_* line failed to parse, which drops the whole block at apply time.
  wificonf_dhcp_t dhcp;
  char address[NETWORK_IPV4_STR_MAX];
  char netmask[NETWORK_IPV4_STR_MAX];
  char gateway[NETWORK_IPV4_STR_MAX];
  char dns[NETWORK_DNS_LIST_STR_MAX];
  bool tcpipInvalid;
} wificonf_t;

/**
 * @brief Parses one line of the file into cfg. Public so a host harness can
 * drive the parser without FatFs.
 *
 * @param cfg  Accumulates the parsed values.
 * @param line One line, modified in place. A trailing CR/LF is removed.
 */
void wificonf_parseLine(wificonf_t *cfg, char *line);

/**
 * @brief Reads WIFICONF_FILE from the mounted card into cfg.
 *
 * @param cfg Zeroed first, then filled from the file.
 * @return true if the file was read, false if it is absent or unreadable.
 */
bool wificonf_readFile(wificonf_t *cfg);

/**
 * @brief Applies cfg to the global settings in RAM. Nothing is written to
 * flash here.
 *
 * The WiFi keys are applied when present. In the TCPIP_* block, no
 * TCPIP_DHCP line leaves the stored setting alone; on stores DHCP on; off is
 * all or nothing, stored only when address, netmask and gateway all parsed
 * and no TCPIP_* line failed, with DNS optional.
 *
 * @param cfg The parsed file.
 * @return true when at least one stored value changed, so the caller knows
 * whether settings_save() is needed.
 */
bool wificonf_applySettings(const wificonf_t *cfg);

/**
 * @brief Is WIFICONF_FILE on the mounted card right now?
 *
 * One f_stat. The Network page and the parameter save handler use it to make
 * the WiFi network and the TCP/IP settings read-only while the file exists,
 * since the file would re-apply them at the next boot anyway.
 */
bool wificonf_isPresent(void);

#endif  // WIFICONF_H
