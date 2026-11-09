/**
 * File: mbedtls_ms_time.c
 * Description: Provides mbedtls_ms_time() for MBEDTLS_PLATFORM_MS_TIME_ALT.
 *
 * mbedTLS needs a monotonic millisecond clock; the SDK's altcp_tls_mbedtls.c
 * will not compile without MBEDTLS_HAVE_TIME. Time since boot is enough for
 * that purpose. This is NOT a wall clock -- certificate validity periods still
 * cannot be checked.
 */

#include "mbedtls/platform_time.h"
#include "pico/time.h"

mbedtls_ms_time_t mbedtls_ms_time(void) {
  return (mbedtls_ms_time_t)to_ms_since_boot(get_absolute_time());
}
