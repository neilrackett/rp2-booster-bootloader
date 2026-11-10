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
  // The candidate needs all three numbers; for the running version, fewer
  // numbers count as zeros.
  CHECK(!vercmp_isNewer("v3", "v2.9.9"));
  CHECK(!vercmp_isNewer("v2.4", "v2.4.0"));
  CHECK(vercmp_isNewer("v2.5.0", "v2.4"));
  // What passes as a version, and what does not.
  CHECK(vercmp_isVersion("v2.5.0"));
  CHECK(vercmp_isVersion("V2.5.0"));
  CHECK(vercmp_isVersion("2.5.0"));
  CHECK(vercmp_isVersion(" v2.5.0\r\n"));
  CHECK(vercmp_isVersion("v2.5.0beta"));
  CHECK(vercmp_isVersion("v2.5.0-rc.1+build.7"));
  CHECK(!vercmp_isVersion("v2.5"));
  CHECK(!vercmp_isVersion("v2..5.0"));
  CHECK(!vercmp_isVersion("v2.5.0 and more"));
  CHECK(!vercmp_isVersion("v2.5.0<html>"));
  CHECK(!vercmp_isVersion("404: Not Found"));
  CHECK(!vercmp_isVersion(""));
  CHECK(!vercmp_isVersion(NULL));
  // No version at all is never newer.
  CHECK(!vercmp_isNewer("", "v2.4.2"));
  CHECK(!vercmp_isNewer(NULL, "v2.4.2"));
  CHECK(vercmp_isNewer("v0.0.1", NULL));
  TEST_END();
}
