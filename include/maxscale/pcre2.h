#pragma once
#ifndef _MAXSCALE_PCRE2_H
#define _MAXSCALE_PCRE2_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 *
 */

/**
 * @file pcre2.h - Utility functions for regular expression matching
 * with the bundled PCRE2 library.
 *
 * @verbatim
 * Revision History
 *
 * Date       Who           Description
 * 30-10-2015 Markus Makela Initial implementation
 * @endverbatim
 */

#include <maxscale/cdefs.h>

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

#include <pcre2.h>

MXS_BEGIN_DECLS

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

MXS_END_DECLS

#endif
