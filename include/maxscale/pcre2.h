/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file pcre2.h - Utility functions for regular expression matching
 * with the bundled PCRE2 library.
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

#if defined (PCRE2_CODE_UNIT_WIDTH)
#error PCRE2_CODE_UNIT_WIDTH already defined. Do not define, and include <maxscale/pcre2.h>.
#else
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

#include <pcre2.h>

/**
 * Print an error message explaining an error code.
 * @param errorcode value returned by pcre2 functions
 */
#define MXS_PCRE2_PRINT_ERROR(errorcode) \
    mxs_pcre2_print_error(errorcode, MXS_MODULE_NAME, __FILE__, __LINE__, __func__)

typedef enum
{
    MXS_PCRE2_MATCH,
    MXS_PCRE2_NOMATCH,
    MXS_PCRE2_ERROR
} mxs_pcre2_result_t;

mxs_pcre2_result_t mxs_pcre2_substitute(pcre2_code* re,
                                        const char* subject,
                                        const char* replace,
                                        char** dest,
                                        size_t* size);
mxs_pcre2_result_t mxs_pcre2_simple_match(const char* pattern,
                                          const char* subject,
                                          int options,
                                          int* error);
/**
 * Print an error message explaining an error code. Best used through the macro
 * MXS_PCRE2_PRINT_ERROR
 */
void mxs_pcre2_print_error(int errorcode,
                           const char* module_name,
                           const char* filename,
                           int line_num,
                           const char* func_name);

/**
 * Check that @c subject is valid. A valid subject matches @c re_match yet does
 * not match @c re_exclude. If an error occurs, an error code is written to
 * @c match_error_out.
 *
 * @param re_match If not NULL, the subject must match this to be valid. If NULL,
 * all inputs are considered valid.
 * @param re_exclude If not NULL, will invalidate a matching subject. Even subjects
 * validated by @c re_match can be invalidated. If NULL, invalidates nothing.
 * @param md PCRE2 match data block
 * @param subject Subject string. Should NOT be an empty string.
 * @param length Length of subject. Can be zero for 0-terminated strings.
 * @param calling_module Which module the function was called from. Can be NULL.
 * Used for log messages.
 *
 * @return True, if subject is considered valid. False if subject is not valid or
 * an error occurred.
 */
bool mxs_pcre2_check_match_exclude(pcre2_code* re_match,
                                   pcre2_code* re_exclude,
                                   pcre2_match_data* md,
                                   const char* subject,
                                   int length,
                                   const char* calling_module);

MXS_END_DECLS
