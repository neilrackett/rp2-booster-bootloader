// units: booster/src/vercmp.c
/* vercmp_isNewer(): the firmware update banner shows when the published
 * SIDECARTVERSION is newer than the running release. Real version strings,
 * and the bodies a misbehaving server or CDN can return instead. */
#include "test.h"
#include "vercmp.h"

int main(void) {
  CHECK(vercmp_isNewer("v2.5.0", "v2.4.2"));
  CHECK(vercmp_isNewer("v2.4.10", "v2.4.9"));
  CHECK(vercmp_isNewer("v3.0.0", "v2.99.99"));
  CHECK(vercmp_isNewer("2.5.0", "v2.4.2"));
  CHECK(vercmp_isNewer("v2.5.0\n", "v2.4.2"));
  CHECK(!vercmp_isNewer("v2.4.2", "v2.4.2"));
  CHECK(!vercmp_isNewer("v2.4.1", "v2.4.2"));
  CHECK(!vercmp_isNewer("v2.4.2", "v2.5.0"));
  // A suffix does not count: a beta of the running version is not newer.
  CHECK(!vercmp_isNewer("v2.4.2beta", "v2.4.2"));
  CHECK(vercmp_isNewer("v2.0.6beta", "v2.0.5"));
  // Fewer numbers count as zeros.
  CHECK(vercmp_isNewer("v3", "v2.9.9"));
  CHECK(!vercmp_isNewer("v2.4", "v2.4.0"));
  // No version at all is never newer.
  CHECK(!vercmp_isNewer("", "v2.4.2"));
  CHECK(!vercmp_isNewer(NULL, "v2.4.2"));
  CHECK(vercmp_isNewer("v0.0.1", NULL));
  TEST_END();
}
