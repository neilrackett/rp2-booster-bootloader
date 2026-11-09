/**
 * File: network_parse.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Parse WiFi credentials and IPv4 settings typed by a user
 */

#ifndef NETWORK_PARSE_H
#define NETWORK_PARSE_H

#include <stdbool.h>

// "255.255.255.255" plus the terminator, and room for the "a,b" list of one
// or two resolvers that PARAM_WIFI_DNS stores (15 + 1 + 15 + 1).
#define NETWORK_IPV4_STR_MAX 16
#define NETWORK_DNS_LIST_STR_MAX (NETWORK_IPV4_STR_MAX * 2)

#define MAX_SSID_LENGTH \
  36  // SSID can have up to 32 characters + null terminator + padding
#define MAX_PASSWORD_LENGTH \
  68  // Password can have up to 64 characters + null terminator + padding
// A WPA/WPA2 passphrase is 8 to 63 printable characters (IEEE 802.11i).
#define NETWORK_WPA_PASSPHRASE_MIN_LENGTH 8
#define NETWORK_WPA_PASSPHRASE_MAX_LENGTH 63

/**
 * @brief Parses and cleans up an SSID string.
 *
 * This function processes the input SSID and ensures it meets the required
 * standard: it is truncated to MAX_SSID_LENGTH if it exceeds the maximum length
 * and cleaned up if it does not comply with the standard format.
 *
 * @param ssid    Pointer to the input null-terminated SSID string.
 * @param outSSID Pointer to the output character array where the parsed and
 * cleaned SSID is stored. The caller is responsible for ensuring this buffer is
 * sufficiently allocated.
 *
 * @return true if the SSID is valid after parsing and cleaning, false
 * otherwise.
 */
bool network_parseSSID(const char* ssid, char* outSSID);

/**
 * @brief Parses and cleans up a WiFi network password from .wificonf.
 *
 * Control characters are dropped and up to NETWORK_WPA_PASSPHRASE_MAX_LENGTH
 * printable characters are copied, spaces included. outPassword always
 * receives what was kept, so the empty PASS of an open network is stored as
 * empty, and a longer value keeps its first 63 characters and returns false.
 *
 * @param password The password text as written in the file.
 * @param outPassword Buffer of at least MAX_PASSWORD_LENGTH bytes.
 * @return true for a passphrase of 8 to 63 printable characters that is not
 * all spaces, false otherwise.
 */
bool network_parsePassword(const char* password, char* outPassword);

/**
 * @brief Parses a strict IPv4 dotted quad meant for a host, a gateway or a
 * resolver.
 *
 * Exactly four decimal octets (0..255) separated by single dots, with leading
 * and trailing blanks tolerated. Narrower on purpose than lwIP's
 * ipaddr_addr(), which also accepts "1.2.3", hexadecimal and octal octets.
 * 0.0.0.0 and 255.255.255.255 are rejected: the first is "any" and the second
 * is both the broadcast address and IPADDR_NONE.
 *
 * @param text       Input text.
 * @param outAddress Buffer of at least NETWORK_IPV4_STR_MAX bytes that
 *                   receives the canonical "a.b.c.d" text on success.
 * @return true if valid, false otherwise (outAddress is then untouched).
 */
bool network_parseIPv4Address(const char* text, char* outAddress);

/**
 * @brief Parses a strict IPv4 netmask.
 *
 * Same grammar as network_parseIPv4Address(), and the value must be
 * contiguous (ones followed by zeros). /0 and /32 are rejected as well:
 * neither leaves room for a gateway on the link.
 *
 * @param text       Input text.
 * @param outNetmask Buffer of at least NETWORK_IPV4_STR_MAX bytes that
 *                   receives the canonical text on success.
 * @return true if valid, false otherwise (outNetmask is then untouched).
 */
bool network_parseIPv4Netmask(const char* text, char* outNetmask);

/**
 * @brief Parses one or two DNS servers separated by a comma, the format
 * PARAM_WIFI_DNS stores.
 *
 * Each entry must satisfy network_parseIPv4Address(). An empty entry, a
 * trailing comma or a third entry makes the whole list invalid.
 *
 * @param text    Input text.
 * @param outList Buffer of at least NETWORK_DNS_LIST_STR_MAX bytes that
 *                receives the canonical "a" or "a,b" text on success.
 * @return true if valid, false otherwise (outList is then untouched).
 */
bool network_parseDnsList(const char* text, char* outList);

// A static TCP/IP configuration in canonical form.
typedef struct {
  char address[NETWORK_IPV4_STR_MAX];
  char netmask[NETWORK_IPV4_STR_MAX];
  char gateway[NETWORK_IPV4_STR_MAX];
  char dns[NETWORK_DNS_LIST_STR_MAX];  // empty when none was given
} network_static_config_t;

/**
 * @brief Checks a whole static TCP/IP configuration, with the rules the
 * .wificonf file follows: address, netmask and gateway must all be valid, and
 * the DNS list too unless it is empty or NULL.
 *
 * @param out Receives the canonical values; only complete on success.
 * @return NULL if the configuration is valid, or the name of the first field
 * that is not, for the error shown to the user.
 */
const char* network_checkStaticConfig(const char* address, const char* netmask,
                                      const char* gateway, const char* dns,
                                      network_static_config_t* out);

#endif  // NETWORK_PARSE_H
