// units: booster/src/vercmp.c
/* The update check compares whatever body the version URL returned. A body
 * that is not a version string never counts as newer: taking the first
 * numbers found anywhere made a "404: Not Found" page version 404.0.0, and an
 * error page served with 200 by a CDN or a captive portal showed the update
 * banner for a release that does not exist. */
#include "test.h"
#include "vercmp.h"

int main(void) {
  CHECK(!vercmp_isNewer("404: Not Found", "v2.5.0"));
  CHECK(!vercmp_isNewer("<html><body>503 Service Unavailable</body></html>",
                        "v2.5.0"));
  CHECK(!vercmp_isNewer("<!DOCTYPE html><title>Login 2024</title>", "v2.5.0"));
  TEST_END();
}
