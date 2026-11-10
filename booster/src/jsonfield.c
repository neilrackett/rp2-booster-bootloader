/**
 * File: jsonfield.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Add a field to a JSON object held in a fixed buffer
 */

#include "jsonfield.h"

#include <stdio.h>
#include <string.h>

bool jsonfield_append_string(char *json, size_t buffer_size, const char *name,
                             const char *value) {
  char *close = strrchr(json, '}');
  if (close == NULL) {
    return false;
  }
  // An empty object takes the member without a leading comma.
  const char *before = close;
  while (before > json && (before[-1] == ' ' || before[-1] == '\t' ||
                           before[-1] == '\r' || before[-1] == '\n')) {
    before--;
  }
  bool empty = before > json && before[-1] == '{';
  char member[160];
  int member_len = snprintf(member, sizeof(member), "%s\"%s\":\"%s\"",
                            empty ? "" : ",", name, value);
  if (member_len < 0 || (size_t)member_len >= sizeof(member)) {
    return false;
  }
  size_t len = strlen(json);
  if (len + (size_t)member_len + 1 > buffer_size) {
    return false;
  }
  memmove(close + member_len, close, strlen(close) + 1);
  memcpy(close, member, (size_t)member_len);
  return true;
}
