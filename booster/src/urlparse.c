/**
 * File: urlparse.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Split an http or https URL into scheme, host, port and path
 */

#include "urlparse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int urlparse_parse(const char *url, url_components_t *components) {
  if (!url || !components) return -1;
  memset(components, 0, sizeof(*components));

  const char *protocol_end = strstr(url, "://");
  if (!protocol_end) return -1;

  size_t protocol_len = (size_t)(protocol_end - url);
  if (protocol_len == 0 || protocol_len >= sizeof(components->protocol))
    return -1;
  memcpy(components->protocol, url, protocol_len);
  components->protocol[protocol_len] = '\0';

  const char *host_start = protocol_end + 3;  // skip ://
  const char *uri_start = strchr(host_start, '/');
  if (uri_start) {
    size_t host_len = (size_t)(uri_start - host_start);
    if (host_len == 0 || host_len >= sizeof(components->host)) return -1;
    memcpy(components->host, host_start, host_len);
    components->host[host_len] = '\0';

    // Copy URI (ensure at least "/")
    if (strlen(uri_start) >= sizeof(components->uri)) return -1;
    strcpy(components->uri, uri_start);
  } else {
    // No path → set host and default uri "/"
    if (strlen(host_start) == 0 ||
        strlen(host_start) >= sizeof(components->host))
      return -1;
    strcpy(components->host, host_start);
    components->uri[0] = '/';
    components->uri[1] = '\0';
  }

  // Split an explicit port off the host ("host:8080"); 0 means "use the
  // scheme default", which httpc resolves to 80 or 443.
  char *port_sep = strchr(components->host, ':');
  if (port_sep != NULL) {
    unsigned long parsed = strtoul(port_sep + 1, NULL, 10);
    if (parsed == 0 || parsed > UINT16_MAX) return -1;
    components->port = (uint16_t)parsed;
    *port_sep = '\0';
  }
  return 0;
}

int urlparse_addCacheBuster(const char *url, uint32_t token, char *out,
                            size_t outSize) {
  if (!out || outSize == 0) return -1;
  out[0] = '\0';
  if (!url) return -1;
  char separator = strchr(url, '?') != NULL ? '&' : '?';
  int written = snprintf(out, outSize, "%s%ccb=%08lx", url, separator,
                         (unsigned long)token);
  if (written < 0 || (size_t)written >= outSize) {
    out[0] = '\0';
    return -1;
  }
  return 0;
}
