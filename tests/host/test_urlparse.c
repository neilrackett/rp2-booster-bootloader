// units: booster/src/urlparse.c
/* urlparse_parse(): the URLs Booster requests (catalog, microfirmware,
 * firmware, version file, redirect targets) and the malformed ones it must
 * refuse rather than truncate. */
#include <string.h>

#include "test.h"
#include "urlparse.h"

static url_components_t c;

int main(void) {
  CHECK_EQ(
      urlparse_parse("https://atarist.sidecartridge.com/SIDECARTVERSION", &c),
      0);
  CHECK(strcmp(c.protocol, "https") == 0);
  CHECK(strcmp(c.host, "atarist.sidecartridge.com") == 0);
  CHECK(strcmp(c.uri, "/SIDECARTVERSION") == 0);
  CHECK_EQ(c.port, 0);

  CHECK_EQ(urlparse_parse("http://192.168.1.10:8080/apps/x.uf2?a=1&b=2", &c),
           0);
  CHECK(strcmp(c.protocol, "http") == 0);
  CHECK(strcmp(c.host, "192.168.1.10") == 0);
  CHECK_EQ(c.port, 8080);
  CHECK(strcmp(c.uri, "/apps/x.uf2?a=1&b=2") == 0);

  // No path: "/" is requested.
  CHECK_EQ(urlparse_parse("https://md-store.sidecartridge.com", &c), 0);
  CHECK(strcmp(c.host, "md-store.sidecartridge.com") == 0);
  CHECK(strcmp(c.uri, "/") == 0);
  CHECK_EQ(urlparse_parse("http://host:65535", &c), 0);
  CHECK_EQ(c.port, 65535);

  // A signed redirect target near the measured worst case (919 characters)
  // and the largest path that fits.
  static char url[2048];
  strcpy(url, "https://objects.githubusercontent.com/");
  size_t prefix = strlen("https://objects.githubusercontent.com");
  memset(url + strlen(url), 'q', 900);
  CHECK_EQ(urlparse_parse(url, &c), 0);
  CHECK_EQ(strlen(c.uri), 901);
  memset(url + prefix, 0, sizeof(url) - prefix);
  url[prefix] = '/';
  memset(url + prefix + 1, 'q', sizeof(c.uri) - 2);
  CHECK_EQ(urlparse_parse(url, &c), 0);
  CHECK_EQ(strlen(c.uri), sizeof(c.uri) - 1);
  // One more character does not fit: refused, not truncated.
  url[prefix + sizeof(c.uri) - 1] = 'q';
  CHECK_EQ(urlparse_parse(url, &c), -1);

  // Host that does not fit, with and without a path.
  memset(url, 0, sizeof(url));
  strcpy(url, "https://");
  memset(url + 8, 'h', sizeof(c.host));
  CHECK_EQ(urlparse_parse(url, &c), -1);
  strcat(url, "/path");
  CHECK_EQ(urlparse_parse(url, &c), -1);

  CHECK_EQ(urlparse_parse("atarist.sidecartridge.com/apps.json", &c), -1);
  CHECK_EQ(urlparse_parse("://host/path", &c), -1);
  CHECK_EQ(urlparse_parse("https:///path", &c), -1);
  CHECK_EQ(urlparse_parse("https://", &c), -1);
  CHECK_EQ(urlparse_parse("averyveryverylongscheme://host/", &c), -1);
  CHECK_EQ(urlparse_parse("http://host:0/x", &c), -1);
  CHECK_EQ(urlparse_parse("http://host:65536/x", &c), -1);
  CHECK_EQ(urlparse_parse("http://host:abc/x", &c), -1);
  CHECK_EQ(urlparse_parse(NULL, &c), -1);
  CHECK_EQ(urlparse_parse("http://host/", NULL), -1);

  // The cache-busting parameter of the firmware update requests.
  char fresh[URLPARSE_FRESH_URL_MAX];
  CHECK_EQ(urlparse_addCacheBuster("https://h/upgrade.bin", 0x1a2b3c4d, fresh,
                                   sizeof(fresh)),
           0);
  CHECK(strcmp(fresh, "https://h/upgrade.bin?cb=1a2b3c4d") == 0);
  CHECK_EQ(urlparse_addCacheBuster("https://h/x", 7, fresh, sizeof(fresh)), 0);
  CHECK(strcmp(fresh, "https://h/x?cb=00000007") == 0);
  CHECK_EQ(urlparse_addCacheBuster("https://h/x?a=1", 1, fresh, sizeof(fresh)),
           0);
  CHECK(strcmp(fresh, "https://h/x?a=1&cb=00000001") == 0);
  // Survives the parser into the request line.
  CHECK_EQ(urlparse_parse(fresh, &c), 0);
  CHECK(strcmp(c.uri, "/x?a=1&cb=00000001") == 0);
  // Exactly fitting, one byte short, and the arguments it must refuse.
  char tight[24];
  CHECK_EQ(urlparse_addCacheBuster("http://h/ab", 1, tight, sizeof(tight)), 0);
  CHECK_EQ(strlen(tight), 23);
  CHECK_EQ(urlparse_addCacheBuster("http://h/abc", 1, tight, sizeof(tight)),
           -1);
  CHECK_EQ(tight[0], '\0');
  CHECK_EQ(urlparse_addCacheBuster(NULL, 1, fresh, sizeof(fresh)), -1);
  CHECK_EQ(fresh[0], '\0');
  CHECK_EQ(urlparse_addCacheBuster("http://h/", 1, NULL, 4), -1);
  CHECK_EQ(urlparse_addCacheBuster("http://h/", 1, fresh, 0), -1);
  TEST_END();
}
