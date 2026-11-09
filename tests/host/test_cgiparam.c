// units: booster/src/cgiparam.c
/* cgiparam_get(): a CGI's parameters as lwIP passes them, including a name
 * given without "=", whose value is NULL. */
#include <stddef.h>

#include "cgiparam.h"
#include "test.h"

int main(void) {
  char *names[] = {"update", "json", "uuid"};
  char *values[] = {"yes", NULL, "44444444-4444-4444-8444-444444444444"};
  CHECK(cgiparam_get("update", 3, names, values) == values[0]);
  CHECK(cgiparam_get("uuid", 3, names, values) == values[2]);
  // "?json" with no "=": present, without a value.
  CHECK(cgiparam_get("json", 3, names, values) == NULL);
  // Missing, no parameters at all, or a count past what was given.
  CHECK(cgiparam_get("id", 3, names, values) == NULL);
  CHECK(cgiparam_get("uuid", 0, names, values) == NULL);
  CHECK(cgiparam_get("uuid", 2, names, values) == NULL);
  CHECK(cgiparam_get(NULL, 3, names, values) == NULL);
  CHECK(cgiparam_get("uuid", 3, NULL, NULL) == NULL);
  // The first of two.
  char *twice[] = {"uuid", "uuid"};
  char *both[] = {"first", "second"};
  CHECK(cgiparam_get("uuid", 2, twice, both) == both[0]);
  TEST_END();
}
