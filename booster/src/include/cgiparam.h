/**
 * File: cgiparam.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Query parameters of a CGI request
 */

#ifndef CGIPARAM_H
#define CGIPARAM_H

/**
 * @brief The value of the query parameter called name.
 *
 * lwIP hands a CGI handler its parameters as two arrays. A parameter given
 * without "=" ("/download.cgi?json") has a NULL value, which a handler must
 * not pass to strlen() or strcmp().
 *
 * @return The first value given for name, or NULL when the request has no
 * such parameter or gives it no value.
 */
const char *cgiparam_get(const char *name, int count, char *const names[],
                         char *const values[]);

#endif  // CGIPARAM_H
