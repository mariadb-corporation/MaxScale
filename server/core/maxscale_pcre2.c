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

#include <maxscale_pcre2.h>

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
 * @param size Size of the desination buffer
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
        while ((rc = pcre2_substitute(re, (PCRE2_SPTR) subject, PCRE2_ZERO_TERMINATED, 0,
                                      PCRE2_SUBSTITUTE_GLOBAL, mdata, NULL,
                                      (PCRE2_SPTR) replace, PCRE2_ZERO_TERMINATED,
                                      (PCRE2_UCHAR*) *dest, size)) == PCRE2_ERROR_NOMEMORY)
        {
            char *tmp = realloc(*dest, *size * 2);
            if (tmp == NULL)
            {
                break;
            }
            *dest = tmp;
            *size *= 2;
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
