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
#include <maxscale/alloc.h>

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
mxs_pcre2_result_t mxs_pcre2_substitute(pcre2_code *re, const char *subject, const char *replace,
                                        char** dest, size_t* size)
{
    int rc;
    mxs_pcre2_result_t rval = MXS_PCRE2_ERROR;
    pcre2_match_data *mdata = pcre2_match_data_create_from_pattern(re, NULL);

    if (mdata)
    {
        size_t size_tmp = *size;
        while ((rc = pcre2_substitute(re, (PCRE2_SPTR) subject, PCRE2_ZERO_TERMINATED, 0,
                                      PCRE2_SUBSTITUTE_GLOBAL, mdata, NULL,
                                      (PCRE2_SPTR) replace, PCRE2_ZERO_TERMINATED,
                                      (PCRE2_UCHAR*) *dest, &size_tmp)) == PCRE2_ERROR_NOMEMORY)
        {
            size_tmp = 2 * (*size);
            char *tmp = MXS_REALLOC(*dest, size_tmp);
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
mxs_pcre2_result_t mxs_pcre2_simple_match(const char* pattern, const char* subject,
                                          int options, int *error)
{
    int err;
    size_t erroff;
    mxs_pcre2_result_t rval = MXS_PCRE2_ERROR;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR) pattern, PCRE2_ZERO_TERMINATED,
                                   options, &err, &erroff, NULL);
    if (re)
    {
        pcre2_match_data *mdata = pcre2_match_data_create_from_pattern(re, NULL);
        if (mdata)
        {
            int rc = pcre2_match(re, (PCRE2_SPTR) subject, PCRE2_ZERO_TERMINATED,
                                 0, 0, mdata, NULL);
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
