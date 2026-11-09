// units: booster/src/textescape.c
/* textescape_js() and textescape_html(): an app name, a catalog URL or a WiFi
 * network name inserted into a page stays text, whatever its quotes. */
#include <string.h>

#include "test.h"
#include "textescape.h"

static char buf[80];

int main(void) {
  CHECK_EQ(textescape_js(buf, sizeof(buf), "Multidrive emulator"), 19);
  CHECK(strcmp(buf, "Multidrive emulator") == 0);
  textescape_js(buf, sizeof(buf), "Diego's Floppy");
  CHECK(strcmp(buf, "Diego\\u0027s Floppy") == 0);
  textescape_js(buf, sizeof(buf), "a\"b`c\\d${x}");
  CHECK(strcmp(buf, "a\\u0022b\\u0060c\\u005Cd\\u0024{x}") == 0);
  textescape_js(buf, sizeof(buf), "</script>&");
  CHECK(strcmp(buf, "\\u003C/script\\u003E\\u0026") == 0);
  textescape_js(buf, sizeof(buf), "line\nbreak\x7f");
  CHECK(strcmp(buf, "line\\u000Abreak\\u007F") == 0);
  textescape_js(buf, sizeof(buf), "Caf\xc3\xa9");  // UTF-8 as it is
  CHECK(strcmp(buf, "Caf\xc3\xa9") == 0);
  textescape_js(buf, sizeof(buf),
                "https://md-store.sidecartridge.com/a.json?c=1");
  CHECK(strcmp(buf, "https://md-store.sidecartridge.com/a.json?c=1") == 0);

  textescape_html(buf, sizeof(buf), "<img src=x onerror=\"alert('1')\"> & co");
  CHECK(strcmp(buf,
               "&lt;img src=x onerror=&quot;alert(&#39;1&#39;)&quot;&gt; "
               "&amp; co") == 0);
  textescape_html(buf, sizeof(buf), "Home WiFi \xc3\xb1");
  CHECK(strcmp(buf, "Home WiFi \xc3\xb1") == 0);

  // Never half an escape, always terminated.
  char tight[8];  // "ab" and a 6-character escape need 9 with the terminator
  CHECK_EQ(textescape_js(tight, sizeof(tight), "ab'cd"), 2);
  CHECK(strcmp(tight, "ab") == 0);
  char small[9];
  CHECK_EQ(textescape_js(small, sizeof(small), "ab'cd"), 8);
  CHECK(strcmp(small, "ab\\u0027") == 0);
  CHECK_EQ(textescape_html(tight, sizeof(tight), "abcd<e"), 4);  // 4 + "&lt;"
  CHECK(strcmp(tight, "abcd") == 0);
  CHECK_EQ(textescape_js(small, 1, "x"), 0);
  CHECK_EQ(small[0], '\0');
  CHECK_EQ(textescape_js(small, sizeof(small), NULL), 0);
  CHECK_EQ(textescape_html(small, sizeof(small), NULL), 0);
  TEST_END();
}
