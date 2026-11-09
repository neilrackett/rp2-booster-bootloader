/**
 * File: httphdr.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Reading the header block of an HTTP response
 */

#include "httphdr.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

bool httphdr_find(const char *headers, const char *name, char *out,
                  size_t out_len) {
  size_t name_len = strlen(name);
  const char *line = headers;
  while (line != NULL && *line != '\0') {
    if (strncasecmp(line, name, name_len) == 0) {
      const char *value = line + name_len;
      while (*value == ' ' || *value == '\t') {
        value++;
      }
      size_t len = strcspn(value, "\r\n");
      if (len == 0 || len >= out_len) {
        return false;  // absent, or too long to use safely
      }
      memcpy(out, value, len);
      out[len] = '\0';
      return true;
    }
    line = strstr(line, "\r\n");
    if (line != NULL) {
      line += 2;
    }
  }
  return false;
}

bool httphdr_is_chunked(const char *headers) {
  char encoding[32];
  if (!httphdr_find(headers, "Transfer-Encoding:", encoding,
                    sizeof(encoding))) {
    return false;
  }
  // Values are case-insensitive, and "chunked" may follow another coding.
  for (char *c = encoding; *c != '\0'; c++) {
    *c = (char)tolower((unsigned char)*c);
  }
  return strstr(encoding, "chunked") != NULL;
}
