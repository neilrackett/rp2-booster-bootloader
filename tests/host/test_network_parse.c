// units: booster/src/network_parse.c
/* The text parsers behind the .wificonf file and the factory WiFi setup:
 * SSID and password clean-up, and the strict IPv4, netmask and DNS list
 * grammar whose canonical output is stored and later handed to lwIP. */
#include <string.h>

#include "network_parse.h"
#include "test.h"

static char out[128];

static int addressIs(const char *text, const char *expected) {
  memset(out, 0, sizeof(out));
  return network_parseIPv4Address(text, out) && strcmp(out, expected) == 0;
}

int main(void) {
  // SSID: printable ASCII kept, control characters dropped, 35 at most.
  CHECK(network_parseSSID("MyNetwork", out) && strcmp(out, "MyNetwork") == 0);
  CHECK(network_parseSSID("My Network 5G", out) &&
        strcmp(out, "My Network 5G") == 0);
  CHECK(network_parseSSID("Tab\there", out) && strcmp(out, "Tabhere") == 0);
  CHECK(!network_parseSSID("", out) && out[0] == '\0');
  CHECK(!network_parseSSID("   ", out));
  CHECK(!network_parseSSID(NULL, out));
  char longSsid[200];
  memset(longSsid, 'S', sizeof(longSsid) - 1);
  longSsid[sizeof(longSsid) - 1] = '\0';
  CHECK(network_parseSSID(longSsid, out));
  CHECK_EQ(strlen(out), MAX_SSID_LENGTH - 1);

  // Password: 8 to 63 printable characters, spaces kept.
  CHECK(network_parsePassword("12345678", out) && strcmp(out, "12345678") == 0);
  CHECK(network_parsePassword("pass word with spaces", out) &&
        strcmp(out, "pass word with spaces") == 0);
  CHECK(!network_parsePassword("1234567", out) && strcmp(out, "1234567") == 0);
  CHECK(!network_parsePassword("", out) && out[0] == '\0');
  CHECK(!network_parsePassword("        ", out));
  char pass[80];
  memset(pass, 'p', 63);
  pass[63] = '\0';
  CHECK(network_parsePassword(pass, out) && strlen(out) == 63);
  // 64 characters: too long for WPA2, the first 63 are kept.
  pass[63] = 'p';
  pass[64] = '\0';
  CHECK(!network_parsePassword(pass, out) && strlen(out) == 63);
  // CR and LF from a Windows file are dropped, not counted.
  CHECK(network_parsePassword("12345678\r\n", out) &&
        strcmp(out, "12345678") == 0);

  // IPv4 address: four decimal octets, canonical output.
  CHECK(addressIs("192.168.1.50", "192.168.1.50"));
  CHECK(addressIs("  10.0.0.1\t", "10.0.0.1"));
  CHECK(addressIs("010.001.000.009", "10.1.0.9"));
  CHECK(!network_parseIPv4Address("0.0.0.0", out));
  CHECK(!network_parseIPv4Address("255.255.255.255", out));
  CHECK(!network_parseIPv4Address("256.1.1.1", out));
  CHECK(!network_parseIPv4Address("1.2.3", out));
  CHECK(!network_parseIPv4Address("1.2.3.4.5", out));
  CHECK(!network_parseIPv4Address("1..2.3", out));
  CHECK(!network_parseIPv4Address("0x7f.0.0.1", out));
  CHECK(!network_parseIPv4Address("1.2.3.4 x", out));
  CHECK(!network_parseIPv4Address("1234.1.1.1", out));
  CHECK(!network_parseIPv4Address("", out));
  CHECK(!network_parseIPv4Address(NULL, out));
  // A failure leaves the output untouched.
  strcpy(out, "unchanged");
  CHECK(!network_parseIPv4Address("bad", out) && strcmp(out, "unchanged") == 0);

  // Netmask: contiguous, not /0 or /32.
  CHECK(network_parseIPv4Netmask("255.255.255.0", out) &&
        strcmp(out, "255.255.255.0") == 0);
  CHECK(network_parseIPv4Netmask("255.255.240.0", out));
  CHECK(network_parseIPv4Netmask("128.0.0.0", out));
  CHECK(network_parseIPv4Netmask("255.255.255.254", out));
  CHECK(!network_parseIPv4Netmask("255.255.255.255", out));
  CHECK(!network_parseIPv4Netmask("0.0.0.0", out));
  CHECK(!network_parseIPv4Netmask("255.0.255.0", out));
  CHECK(!network_parseIPv4Netmask("255.255.255.1", out));

  // DNS: one or two addresses.
  CHECK(network_parseDnsList("8.8.8.8", out) && strcmp(out, "8.8.8.8") == 0);
  CHECK(network_parseDnsList("192.168.1.1, 8.8.8.8", out) &&
        strcmp(out, "192.168.1.1,8.8.8.8") == 0);
  CHECK(!network_parseDnsList("1.1.1.1,", out));
  CHECK(!network_parseDnsList(",1.1.1.1", out));
  CHECK(!network_parseDnsList("1.1.1.1,8.8.8.8,9.9.9.9", out));
  CHECK(!network_parseDnsList("1.1.1.1,0.0.0.0", out));
  char longHead[100];
  memset(longHead, '1', 60);
  strcpy(longHead + 60, ",8.8.8.8");
  CHECK(!network_parseDnsList(longHead, out));
  // The longest list fits its buffer exactly.
  char list[NETWORK_DNS_LIST_STR_MAX];
  CHECK(network_parseDnsList("255.255.255.254,255.255.255.254", list));
  CHECK_EQ(strlen(list), NETWORK_DNS_LIST_STR_MAX - 1);
  TEST_END();
}
