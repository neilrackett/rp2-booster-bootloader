/**
 * File: jsonfield.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Add a field to a JSON object held in a fixed buffer
 */

#ifndef JSONFIELD_H
#define JSONFIELD_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Adds "name":"value" as the last member of the JSON object in json,
 * just before its closing brace, with a comma unless the object is empty.
 *
 * name and value are written as given, so they must not need escaping.
 * Nothing changes when json has no closing brace or the result, with its
 * terminator, would not fit in buffer_size bytes.
 *
 * @return true if the field was added.
 */
bool jsonfield_append_string(char *json, size_t buffer_size, const char *name,
                             const char *value);

#endif  // JSONFIELD_H
