/**
 * File: urlparse.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Split an http or https URL into scheme, host, port and path
 */

#ifndef URLPARSE_H
#define URLPARSE_H

#include <stddef.h>
#include <stdint.h>

// Room for a Booster OTA URL once urlparse_addCacheBuster() has extended it.
#define URLPARSE_FRESH_URL_MAX 160

typedef struct {
  char protocol[16];
  char host[128];
  // A redirect target can be far longer than the URL originally requested.
  // GitHub release assets end at a signed storage URL whose query string alone
  // runs past 900 characters (SAS token plus JWT), so 256 truncated it and the
  // follow-up request went to a mangled path.
  char uri[1536];
  // Explicit port from a "host:port" URL, or 0 to let the scheme decide
  // (80 for http, 443 for https).
  uint16_t port;
} url_components_t;

/**
 * @brief Splits url ("scheme://host[:port][/path]") into components.
 *
 * Refuses rather than truncates: a scheme, host or path that does not fit
 * its field is an error, because a silently shortened path is a request for
 * the wrong resource. The measured worst case is a GitHub release asset at
 * 919 characters. A URL with no path gets "/". An empty scheme or host, and a
 * port that is 0 or above 65535, are errors.
 *
 * @return 0 on success, -1 on any error. components is zeroed first.
 */
int urlparse_parse(const char *url, url_components_t *components);

/**
 * @brief Writes url to out with a "cb" query parameter holding token.
 *
 * A CDN caches by the whole URL, so a request with a parameter no other
 * request used cannot be answered from its cache. Uses "&" if url already has
 * a query.
 *
 * @return 0 on success, -1 if an argument is NULL or the result does not fit
 * outSize. On failure out holds an empty string, if it has room for one.
 */
int urlparse_addCacheBuster(const char *url, uint32_t token, char *out,
                            size_t outSize);

#endif  // URLPARSE_H
