/**
 * File: httphdr.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Reading the header block of an HTTP response
 */

#ifndef HTTPHDR_H
#define HTTPHDR_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Find a header's value by name, case-insensitively.
 *
 * Ported from md-browser's download.c. Walks CRLF-delimited lines rather than
 * using strstr on the whole block, so a header name appearing inside another
 * header's value cannot match.
 *
 * @param headers The header block, NUL-terminated.
 * @param name The name with its colon, such as "Location:".
 * @param out Out: the value, without leading spaces or tabs.
 * @return true when found; false when absent, empty, or too long for out.
 */
bool httphdr_find(const char *headers, const char *name, char *out,
                  size_t out_len);

/**
 * @brief Is the body sent with Transfer-Encoding: chunked?
 *
 * The download writes the body to a file as it arrives, so chunk framing
 * would end up in the file.
 */
bool httphdr_is_chunked(const char *headers);

#endif  // HTTPHDR_H
