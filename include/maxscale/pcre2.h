#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
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
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

#if defined(PCRE2_CODE_UNIT_WIDTH)
#error PCRE2_CODE_UNIT_WIDTH already defined. Do not define, and include <maxscale/pcre2.h>.
#else
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

#include <pcre2.h>

/**
 * Print an error message explaining an error code.
 * @param errorcode value returned by pcre2 functions
 */
#define MXS_PCRE2_PRINT_ERROR(errorcode)\
    mxs_pcre2_print_error(errorcode, MXS_MODULE_NAME, __FILE__,__LINE__, __func__)

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
/**
 * Print an error message explaining an error code. Best used through the macro
 * MXS_PCRE2_PRINT_ERROR
 */
void mxs_pcre2_print_error(int errorcode, const char *module_name, const char *filename,
                           int line_num, const char* func_name);

MXS_END_DECLS
