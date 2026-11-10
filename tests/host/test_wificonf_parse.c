// units: booster/src/wificonf_parse.c booster/src/network_parse.c
/* The .wificonf line parser on the README's examples, with Windows line
 * endings, comments, every TCPIP_DHCP spelling, and the lines that must
 * flag the TCP/IP block invalid so it is dropped as a whole. */
#include <string.h>

#include "test.h"
#include "wificonf.h"

static wificonf_t cfg;

static void parseAll(const char *text) {
  memset(&cfg, 0, sizeof(cfg));
  char copy[1024];
  strncpy(copy, text, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';
  for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
    char buf[WIFICONF_LINE_MAX];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    wificonf_parseLine(&cfg, buf);
  }
}

static wificonf_dhcp_t dhcpOf(const char *value) {
  char line[64];
  memset(&cfg, 0, sizeof(cfg));
  snprintf(line, sizeof(line), "TCPIP_DHCP=%s", value);
  wificonf_parseLine(&cfg, line);
  return cfg.dhcp;
}

int main(void) {
  parseAll("SSID=MyNetwork\r\nPASS=MyPassword\r\nAUTH=5\r\n");
  CHECK(cfg.hasSsid && strcmp(cfg.ssid, "MyNetwork") == 0);
  CHECK(cfg.hasPass && strcmp(cfg.pass, "MyPassword") == 0);
  CHECK(cfg.hasAuth && cfg.auth == 5);
  CHECK_EQ(cfg.dhcp, WIFICONF_DHCP_UNSET);
  CHECK(!cfg.tcpipInvalid);

  parseAll(
      "# static example\n"
      "SSID=MyNetwork\n"
      "PASS=MyPassword\n"
      "AUTH=5\n"
      "TCPIP_DHCP=No\n"
      "TCPIP_ADDRESS=192.168.1.50\n"
      "TCPIP_NETMASK=255.255.255.0\n"
      "TCPIP_GATEWAY=192.168.1.1\n"
      "TCPIP_DNS=192.168.1.1,8.8.8.8\n");
  CHECK_EQ(cfg.dhcp, WIFICONF_DHCP_OFF);
  CHECK(strcmp(cfg.address, "192.168.1.50") == 0);
  CHECK(strcmp(cfg.netmask, "255.255.255.0") == 0);
  CHECK(strcmp(cfg.gateway, "192.168.1.1") == 0);
  CHECK(strcmp(cfg.dns, "192.168.1.1,8.8.8.8") == 0);
  CHECK(!cfg.tcpipInvalid);

  // Keys in any case, blanks around TCP/IP keys and values, comments,
  // unknown keys and lines with no '=' are ignored.
  parseAll(
      "; comment\n"
      "  # indented comment\n"
      "tcpip_address = 10.0.0.2 \n"
      "Colour=blue\n"
      "no separator here\n"
      "\n");
  CHECK(strcmp(cfg.address, "10.0.0.2") == 0);
  CHECK(!cfg.hasSsid && !cfg.hasPass && !cfg.tcpipInvalid);

  // SSID and PASS keep the text after '=' as written, spaces included.
  parseAll("SSID= Cafe Wifi \nPASS= secret pass \n");
  CHECK(strcmp(cfg.ssid, " Cafe Wifi ") == 0);
  CHECK(strcmp(cfg.pass, " secret pass ") == 0);
  // An empty PASS is an open network.
  parseAll("PASS=\n");
  CHECK(cfg.hasPass && cfg.pass[0] == '\0');

  // Every spelling of TCPIP_DHCP, and one that is none of them.
  const char *on[] = {"1", "True", "yes", "T", "y", "YES"};
  const char *off[] = {"0", "false", "No", "f", "N", "NO"};
  for (size_t i = 0; i < 6; i++) {
    CHECK_EQ(dhcpOf(on[i]), WIFICONF_DHCP_ON);
    CHECK_EQ(dhcpOf(off[i]), WIFICONF_DHCP_OFF);
  }
  CHECK_EQ(dhcpOf("maybe"), WIFICONF_DHCP_UNSET);
  CHECK_EQ(dhcpOf(""), WIFICONF_DHCP_UNSET);
  CHECK_EQ(dhcpOf(" no "), WIFICONF_DHCP_OFF);

  // A malformed TCP/IP line empties its field and flags the block.
  parseAll(
      "TCPIP_DHCP=No\nTCPIP_ADDRESS=192.168.1\nTCPIP_NETMASK=255.255.255.0\n");
  CHECK(cfg.tcpipInvalid);
  CHECK(cfg.address[0] == '\0');
  CHECK(strcmp(cfg.netmask, "255.255.255.0") == 0);
  parseAll("TCPIP_NETMASK=255.0.255.0\n");
  CHECK(cfg.tcpipInvalid);
  parseAll("TCPIP_DNS=8.8.8.8,\n");
  CHECK(cfg.tcpipInvalid && cfg.dns[0] == '\0');

  // A line as long as the reader's buffer, with every key: no write past a
  // field (the address sanitizer would abort).
  const char *keys[] = {
      "SSID=", "PASS=", "AUTH=", "TCPIP_DHCP=", "TCPIP_ADDRESS=", "TCPIP_DNS="};
  for (size_t k = 0; k < 6; k++) {
    char line[WIFICONF_LINE_MAX];
    memset(&cfg, 0, sizeof(cfg));
    size_t keyLen = strlen(keys[k]);
    memcpy(line, keys[k], keyLen);
    memset(line + keyLen, '7', sizeof(line) - keyLen - 1);
    line[sizeof(line) - 1] = '\0';
    wificonf_parseLine(&cfg, line);
  }
  CHECK_EQ(strlen(cfg.dns), 0);
  TEST_END();
}
