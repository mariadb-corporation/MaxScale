#ifndef _MAXSCALE_PCRE2_H
#define _MAXSCALE_PCRE2_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2015
 *
 */

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

#include <pcre2.h>

/**
 * @file maxscale_pcre2.h - Utility functions for regular expression matching
 * with the bundled PCRE2 library.
 *
 * @verbatim
 * Revision History
 *
 * Date       Who           Description
 * 30-10-2015 Markus Makela Initial implementation
 * @endverbatim
 */

typedef enum
{
    MXS_PCRE2_MATCH,
    MXS_PCRE2_NOMATCH,
    MXS_PCRE2_ERROR
} mxs_pcre2_result_t;

mxs_pcre2_result_t mxs_pcre2_substitute(pcre2_code *re, const char *subject,
                                        const char *replace, char** dest, size_t* size);
mxs_pcre2_result_t mxs_pcre2_simple_match(const char* pattern, const char* subject,
                                          int options, int* error);

#endif
