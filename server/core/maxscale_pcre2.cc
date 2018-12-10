/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 *
 */

/**
 * @file maxscale_pcre2.c - Utility functions for regular expression matching
 * with the bundled PCRE2 library.
 *
 * @verbatim
 * Revision History
 *
 * Date       Who           Description
 * 30-10-2015 Markus Makela Initial implementation
 * @endverbatim
 */

#include <maxscale/pcre2.h>
#include <maxbase/assert.h>
#include <maxscale/alloc.h>
#include <maxscale/log.hh>

/**
 * Utility wrapper for PCRE2 library function call pcre2_substitute.
 *
 * This function replaces all occurences of a pattern with the provided replacement
 * and places the end result into @c dest. This buffer must be allocated by the caller.
 * If the size of @c dest is not large enough it will be reallocated to a larger size.
 * The size of @c dest is stored in @c size if any reallocation takes place.
 *
 * @param re Compiled pattern to use
 * @param subject Subject string
 * @param replace Replacement string
 * @param dest Destination buffer
 * @param size Size of the destination buffer
 * @return MXS_PCRE2_MATCH if replacements were made, MXS_PCRE2_NOMATCH if nothing
 * was replaced or MXS_PCRE2_ERROR if memory reallocation failed
 */
mxs_pcre2_result_t mxs_pcre2_substitute(pcre2_code* re,
                                        const char* subject,
                                        const char* replace,
                                        char** dest,
                                        size_t* size)
{
    int rc;
    mxs_pcre2_result_t rval = MXS_PCRE2_ERROR;
    pcre2_match_data* mdata = pcre2_match_data_create_from_pattern(re, NULL);

    if (mdata)
    {
        size_t size_tmp = *size;
        while ((rc = pcre2_substitute(re,
                                      (PCRE2_SPTR) subject,
                                      PCRE2_ZERO_TERMINATED,
                                      0,
                                      PCRE2_SUBSTITUTE_GLOBAL,
                                      mdata,
                                      NULL,
                                      (PCRE2_SPTR) replace,
                                      PCRE2_ZERO_TERMINATED,
                                      (PCRE2_UCHAR*) *dest,
                                      &size_tmp)) == PCRE2_ERROR_NOMEMORY)
        {
            size_tmp = 2 * (*size);
            char* tmp = (char*)MXS_REALLOC(*dest, size_tmp);
            if (tmp == NULL)
            {
                break;
            }
            *dest = tmp;
            *size = size_tmp;
        }

        if (rc > 0)
        {
            rval = MXS_PCRE2_MATCH;
        }
        else if (rc == 0)
        {
            rval = MXS_PCRE2_NOMATCH;
        }
        pcre2_match_data_free(mdata);
    }

    return rval;
}

/**
 * Do a simple matching of a pattern to a string.
 *
 * This function compiles the given pattern and checks if the subject string matches
 * it.
 * @param pattern Pattern used for matching
 * @param subject Subject string to match
 * @param options PCRE2 compilation options
 * @param error The PCRE2 error code is stored here if one is available
 * @return MXS_PCRE2_MATCH if @c subject matches @c pattern, MXS_PCRE2_NOMATCH if
 * they do not match and MXS_PCRE2_ERROR if an error occurred. If an error occurred
 * within the PCRE2 library, @c error will contain the error code. Otherwise it is
 * set to 0.
 */
mxs_pcre2_result_t mxs_pcre2_simple_match(const char* pattern,
                                          const char* subject,
                                          int options,
                                          int* error)
{
    int err;
    size_t erroff;
    mxs_pcre2_result_t rval = MXS_PCRE2_ERROR;
    pcre2_code* re = pcre2_compile((PCRE2_SPTR) pattern,
                                   PCRE2_ZERO_TERMINATED,
                                   options,
                                   &err,
                                   &erroff,
                                   NULL);
    if (re)
    {
        pcre2_match_data* mdata = pcre2_match_data_create_from_pattern(re, NULL);
        if (mdata)
        {
            int rc = pcre2_match(re,
                                 (PCRE2_SPTR) subject,
                                 PCRE2_ZERO_TERMINATED,
                                 0,
                                 0,
                                 mdata,
                                 NULL);
            if (rc == PCRE2_ERROR_NOMATCH)
            {
                rval = MXS_PCRE2_NOMATCH;
            }
            else if (rc > 0)
            {
                /** Since we used the pattern to create the matching data,
                 * pcre2_match will never return 0 */
                rval = MXS_PCRE2_MATCH;
            }
            pcre2_match_data_free(mdata);
        }
        else
        {
            *error = 0;
        }
        pcre2_code_free(re);
    }
    else
    {
        *error = err;
    }
    return rval;
}

void mxs_pcre2_print_error(int errorcode,
                           const char* module_name,
                           const char* filename,
                           int line_num,
                           const char* func_name)
{
    mxb_assert(filename);
    mxb_assert(func_name);
    if (mxs_log_is_priority_enabled(LOG_ERR))
    {
        // 120 should be enough to contain any error message according to pcre2 manual.
        const PCRE2_SIZE errbuf_len = 120;
        PCRE2_UCHAR errorbuf[errbuf_len];
        pcre2_get_error_message(errorcode, errorbuf, errbuf_len);
        mxs_log_message(LOG_ERR,
                        module_name,
                        filename,
                        line_num,
                        func_name,
                        "PCRE2 Error message: '%s'.",
                        errorbuf);
    }
}

bool mxs_pcre2_check_match_exclude(pcre2_code* re_match,
                                   pcre2_code* re_exclude,
                                   pcre2_match_data* md,
                                   const char* subject,
                                   int length,
                                   const char* calling_module)
{
    mxb_assert((!re_match && !re_exclude) || (md && subject));
    bool rval = true;
    int string_len = ((size_t)length == PCRE2_ZERO_TERMINATED) ? strlen(subject) : length;
    if (re_match)
    {
        int result = pcre2_match(re_match, (PCRE2_SPTR)subject, string_len, 0, 0, md, NULL);
        if (result == PCRE2_ERROR_NOMATCH)
        {
            rval = false;   // Didn't match the "match"-regex
            if (mxs_log_is_priority_enabled(LOG_INFO))
            {
                mxs_log_message(LOG_INFO,
                                calling_module,
                                __FILE__,
                                __LINE__,
                                __func__,
                                "Subject does not match the 'match' pattern: %.*s",
                                string_len,
                                subject);
            }
        }
        else if (result < 0)
        {
            rval = false;
            /* The __FILE__ etc macros here do not match calling_module, but
             * the values are only used for throttling messages. */
            mxs_pcre2_print_error(result, calling_module, __FILE__, __LINE__, __func__);
        }
    }
    if (rval && re_exclude)
    {
        int result = pcre2_match(re_exclude, (PCRE2_SPTR)subject, string_len, 0, 0, md, NULL);
        if (result >= 0)
        {
            rval = false;   // Matched the "exclude"-regex
            if (mxs_log_is_priority_enabled(LOG_INFO))
            {
                mxs_log_message(LOG_INFO,
                                calling_module,
                                __FILE__,
                                __LINE__,
                                __func__,
                                "Query matches the 'exclude' pattern: %.*s",
                                string_len,
                                subject);
            }
        }
        else if (result != PCRE2_ERROR_NOMATCH)
        {
            rval = false;
            mxs_pcre2_print_error(result, calling_module, __FILE__, __LINE__, __func__);
        }
    }
    return rval;
}
