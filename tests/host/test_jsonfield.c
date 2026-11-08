// units: booster/src/jsonfield.c
/* jsonfield_append_string(): the Apps page learns that an app's settings are
 * on the card from a "settings":"card" member the device adds to the app's
 * JSON as it is read. The JSON sits in a fixed buffer, so the member is added
 * only when it fits, and an empty object takes no comma. */
#include <stdlib.h>
#include <string.h>

#include "jsonfield.h"
#include "test.h"

int main(void) {
  char json[96];
  strcpy(json, "{\"uuid\":\"x\",\"name\":\"App\"}");
  CHECK(jsonfield_append_string(json, sizeof(json), "settings", "card"));
  CHECK(strcmp(json,
               "{\"uuid\":\"x\",\"name\":\"App\",\"settings\":\"card\"}") == 0);

  // Whitespace and a newline after the object, as a file can end.
  strcpy(json, "{\"a\":1}\n");
  CHECK(jsonfield_append_string(json, sizeof(json), "settings", "card"));
  CHECK(strcmp(json, "{\"a\":1,\"settings\":\"card\"}\n") == 0);

  // An empty object.
  strcpy(json, "{ }");
  CHECK(jsonfield_append_string(json, sizeof(json), "settings", "card"));
  CHECK(strcmp(json, "{ \"settings\":\"card\"}") == 0);

  // Exactly fits: 7 + 20 characters and the terminator in 28 bytes, on the
  // heap so the address sanitizer catches a write past it.
  const char *base = "{\"a\":1}";
  size_t need = strlen(base) + strlen(",\"settings\":\"card\"") + 1;
  char *exact = malloc(need);
  strcpy(exact, base);
  CHECK(jsonfield_append_string(exact, need, "settings", "card"));
  CHECK_EQ(strlen(exact), need - 1);
  free(exact);

  // One byte short: unchanged.
  char *tight = malloc(need - 1);
  strcpy(tight, base);
  CHECK(!jsonfield_append_string(tight, need - 1, "settings", "card"));
  CHECK(strcmp(tight, base) == 0);
  free(tight);

  // Not an object: unchanged.
  strcpy(json, "not json");
  CHECK(!jsonfield_append_string(json, sizeof(json), "settings", "card"));
  CHECK(strcmp(json, "not json") == 0);
  TEST_END();
}
