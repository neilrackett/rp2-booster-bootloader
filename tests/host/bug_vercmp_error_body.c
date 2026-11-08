// units: booster/src/vercmp.c
/* The update check compares whatever body the version URL returned. A body
 * that is not a version string must never count as newer, but the first
 * numbers found anywhere are taken as the version: a "404: Not Found" page
 * is version 404.0.0, and every device shows the update banner for a release
 * that does not exist. The download path accepts any HTTP 200 body, so an
 * error page served with 200 by a CDN or a captive portal does this. */
#include "test.h"
#include "vercmp.h"

int main(void) {
  CHECK(!vercmp_isNewer("404: Not Found", "v2.5.0"));
  CHECK(!vercmp_isNewer("<html><body>503 Service Unavailable</body></html>",
                        "v2.5.0"));
  CHECK(!vercmp_isNewer("<!DOCTYPE html><title>Login 2024</title>", "v2.5.0"));
  TEST_END();
}
