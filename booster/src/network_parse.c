/**
 * File: network_parse.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Parse WiFi credentials and IPv4 settings typed by a user
 */

#include "network_parse.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Function to parse and clean up an SSID string
// If the SSID is longer than MAX_SSID_LENGTH, it is truncated.
// If the SSID does not comply with the standard, it is cleaned up.
// Returns true if valid, false otherwise.
bool network_parseSSID(const char *ssid, char *outSSID) {
  if (ssid == NULL || outSSID == NULL) {
    return false;
  }

  // IEEE 802.11 SSID rules:
  //  - Length: 1..32 bytes (MAX_SSID_LENGTH must be <= 33)
  //  - May not be all spaces
  //  - Cannot have control/non-printable chars (typically 0x20..0x7E allowed)

  size_t inLen = strnlen(ssid, MAX_SSID_LENGTH * 2);  // catch crazy input
  if (inLen == 0) {
    outSSID[0] = '\0';
    return false;
  }

  // Clean: Copy only allowed chars up to MAX_SSID_LENGTH-1
  size_t outLen = 0;
  for (size_t i = 0; i < inLen && outLen < MAX_SSID_LENGTH - 1; ++i) {
    char c = ssid[i];
    // Only printable ASCII (0x20-0x7E), disallow control characters
    if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) {
      outSSID[outLen++] = c;
    }
  }
  outSSID[outLen] = '\0';

  // Check: Not empty, not all spaces, not all filtered out
  if (outLen == 0) return false;
  for (size_t i = 0; i < outLen; ++i) {
    if (outSSID[i] != ' ') return true;
  }
  return false;
}

// Parse and clean up a WiFi password from .wificonf, its only caller.
//
// The copy used to be bounded by WIFI_AP_PASS_MAX_LENGTH, which sizes the
// factory access point's own 8-character password, so every password in the
// file was cut to its first 8 characters and any longer WPA2 password failed
// to connect. That dates from v2.0.6beta. The bound is now the WPA2 limit of
// 63 characters, since v2.4.2.
//
// Returns true for a passphrase of 8 to 63 printable characters that is not
// all spaces. outPassword always receives what was kept: an open network's
// empty PASS is stored as empty, and a value longer than 63 characters keeps
// its first 63 and is reported invalid, since no WPA2 network accepts it.
bool network_parsePassword(const char *password, char *outPassword) {
  if (password == NULL || outPassword == NULL) {
    return false;
  }

  // IEEE 802.11 password rules:
  //  - WPA/WPA2 passphrase: 8 to 63 characters
  //  - Only printable ASCII chars (0x20-0x7E) are valid
  //  - Not all spaces

  size_t inLen =
      strnlen(password, MAX_PASSWORD_LENGTH * 2);  // catch crazy input
  if (inLen == 0) {
    outPassword[0] = '\0';
    return false;
  }

  // Copy only printable characters, up to the passphrase maximum. A printable
  // character that does not fit marks the value as too long.
  enum { PRINTABLE_FIRST = 0x20, PRINTABLE_LAST = 0x7E };
  size_t outLen = 0;
  bool tooLong = false;
  for (size_t i = 0; i < inLen; ++i) {
    unsigned char chr = (unsigned char)password[i];
    if (chr < PRINTABLE_FIRST || chr > PRINTABLE_LAST) {
      continue;
    }
    if (outLen >= NETWORK_WPA_PASSPHRASE_MAX_LENGTH) {
      tooLong = true;
      break;
    }
    outPassword[outLen++] = (char)chr;
  }
  outPassword[outLen] = '\0';

  if (outLen == 0 || tooLong) {
    return false;
  }
  for (size_t i = 0; i < outLen; ++i) {
    if (outPassword[i] != ' ') {
      return outLen >= NETWORK_WPA_PASSPHRASE_MIN_LENGTH;
    }
  }
  return false;  // all spaces
}

// IPv4 text parsers for the .wificonf TCPIP_* block. They
// read strictly and write canonical text, because what they produce is
// stored and later handed to ipaddr_addr() by network_wifiStaConnect():
// "010" stored as typed would come back as octal 8.
// NOLINTBEGIN(readability-magic-numbers)

// Exactly four decimal octets 0..255 separated by single dots, leading and
// trailing blanks tolerated, nothing else. Deliberately narrower than lwIP's
// ipaddr_addr(), which reads "1.2.3" as 1.2.0.3 and accepts hexadecimal and
// octal octets. A configuration file should get none of that.
static bool network_readDottedQuad(const char *text, uint8_t octets[4]) {
  if (text == NULL) {
    return false;
  }
  const char *cursor = text;
  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }
  for (int i = 0; i < 4; i++) {
    if (!isdigit((unsigned char)*cursor)) {
      return false;
    }
    unsigned int value = 0;
    int digits = 0;
    while (isdigit((unsigned char)*cursor)) {
      value = (value * 10) + (unsigned int)(*cursor - '0');
      digits++;
      if (digits > 3 || value > 255) {
        return false;
      }
      cursor++;
    }
    octets[i] = (uint8_t)value;
    if (i < 3) {
      if (*cursor != '.') {
        return false;
      }
      cursor++;
    }
  }
  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }
  return *cursor == '\0';
}

static uint32_t network_dottedQuadValue(const uint8_t octets[4]) {
  return ((uint32_t)octets[0] << 24) | ((uint32_t)octets[1] << 16) |
         ((uint32_t)octets[2] << 8) | (uint32_t)octets[3];
}

static void network_writeDottedQuad(const uint8_t octets[4], char *out) {
  snprintf(out, NETWORK_IPV4_STR_MAX, "%u.%u.%u.%u", octets[0], octets[1],
           octets[2], octets[3]);
}

bool network_parseIPv4Address(const char *text, char *outAddress) {
  uint8_t octets[4] = {0};
  if (outAddress == NULL || !network_readDottedQuad(text, octets)) {
    return false;
  }
  uint32_t value = network_dottedQuadValue(octets);
  // 0.0.0.0 is "any"; 255.255.255.255 is the broadcast address and also
  // IPADDR_NONE, the value ipaddr_addr() returns on failure. Neither can be a
  // host, a gateway or a resolver.
  if (value == 0 || value == 0xFFFFFFFFU) {
    return false;
  }
  network_writeDottedQuad(octets, outAddress);
  return true;
}

bool network_parseIPv4Netmask(const char *text, char *outNetmask) {
  uint8_t octets[4] = {0};
  if (outNetmask == NULL || !network_readDottedQuad(text, octets)) {
    return false;
  }
  uint32_t mask = network_dottedQuadValue(octets);
  // A contiguous mask is ones followed by zeros, so its complement is zeros
  // followed by ones, and a number of that shape ANDed with its successor is
  // zero. /0 and /32 are rejected too: neither leaves room for a gateway on
  // the link.
  uint32_t inverse = ~mask;
  if (mask == 0 || mask == 0xFFFFFFFFU || (inverse & (inverse + 1)) != 0) {
    return false;
  }
  network_writeDottedQuad(octets, outNetmask);
  return true;
}

bool network_parseDnsList(const char *text, char *outList) {
  if (text == NULL || outList == NULL) {
    return false;
  }
  char first[NETWORK_IPV4_STR_MAX] = {0};
  char second[NETWORK_IPV4_STR_MAX] = {0};
  const char *comma = strchr(text, ',');
  if (comma == NULL) {
    if (!network_parseIPv4Address(text, first)) {
      return false;
    }
    snprintf(outList, NETWORK_DNS_LIST_STR_MAX, "%s", first);
    return true;
  }
  // Two entries. Copy the head so it can be terminated without touching the
  // caller's text; a head that does not fit cannot be an address anyway.
  char head[NETWORK_IPV4_STR_MAX * 2] = {0};
  size_t headLen = (size_t)(comma - text);
  if (headLen >= sizeof(head)) {
    return false;
  }
  memcpy(head, text, headLen);
  head[headLen] = '\0';
  if (strchr(comma + 1, ',') != NULL) {
    return false;  // three or more entries
  }
  if (!network_parseIPv4Address(head, first) ||
      !network_parseIPv4Address(comma + 1, second)) {
    return false;
  }
  snprintf(outList, NETWORK_DNS_LIST_STR_MAX, "%s,%s", first, second);
  return true;
}
// NOLINTEND(readability-magic-numbers)
