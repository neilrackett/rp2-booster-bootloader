/**
 * File: textescape.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Text from outside the device inserted into a page
 */

#include "textescape.h"

#include <stdbool.h>
#include <string.h>

// Appends text to dst unless it does not fit, leaving room for the terminator.
static bool textescape_put(char *dst, size_t dstSize, size_t *out,
                           const char *text, size_t len) {
  if (*out + len >= dstSize) {
    return false;
  }
  memcpy(dst + *out, text, len);
  *out += len;
  return true;
}

size_t textescape_js(char *dst, size_t dstSize, const char *src) {
  static const char hex[] = "0123456789ABCDEF";
  size_t out = 0;
  if (dstSize == 0) {
    return 0;
  }
  for (const unsigned char *p = (const unsigned char *)src; src && *p; p++) {
    unsigned char c = *p;
    bool escape = c < 0x20 || c == 0x7F || c == '"' || c == '\'' || c == '`' ||
                  c == '\\' || c == '$' || c == '<' || c == '>' || c == '&';
    char seq[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 0x0F]};
    if (!textescape_put(dst, dstSize, &out, escape ? seq : (const char *)p,
                        escape ? sizeof(seq) : 1)) {
      break;
    }
  }
  dst[out] = '\0';
  return out;
}

size_t textescape_html(char *dst, size_t dstSize, const char *src) {
  size_t out = 0;
  if (dstSize == 0) {
    return 0;
  }
  for (const char *p = src; src && *p; p++) {
    const char *ref = NULL;
    switch (*p) {
      case '&':
        ref = "&amp;";
        break;
      case '<':
        ref = "&lt;";
        break;
      case '>':
        ref = "&gt;";
        break;
      case '"':
        ref = "&quot;";
        break;
      case '\'':
        ref = "&#39;";
        break;
      default:
        break;
    }
    if (!textescape_put(dst, dstSize, &out, ref ? ref : p,
                        ref ? strlen(ref) : 1)) {
      break;
    }
  }
  dst[out] = '\0';
  return out;
}
