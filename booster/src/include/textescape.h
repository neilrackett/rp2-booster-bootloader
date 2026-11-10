/**
 * File: textescape.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Text from outside the device inserted into a page
 */

#ifndef TEXTESCAPE_H
#define TEXTESCAPE_H

#include <stddef.h>

// App names, catalog URLs and WiFi network names come from catalogs, users and
// whoever runs a nearby access point. Inserted into a page as they are, a
// quote ends the string they are in, and markup becomes part of the page.
//
// Both functions copy src into dst, return the length written, stop before an
// escape that does not fit, and always terminate dst when dstSize is not 0.
// Bytes from 0x80 up, UTF-8, are copied as they are.

/**
 * @brief Escapes for use inside a JavaScript or JSON string literal quoted
 * with ', " or `.
 *
 * The quotes, the backslash, '$' (a template literal's "${"), '<', '>' and '&'
 * (a "</script>"), and control characters become \u00XX escapes.
 */
size_t textescape_js(char *dst, size_t dstSize, const char *src);

/**
 * @brief Escapes for use as HTML text or a quoted attribute value: & < > " '
 * become character references.
 */
size_t textescape_html(char *dst, size_t dstSize, const char *src);

#endif  // TEXTESCAPE_H
