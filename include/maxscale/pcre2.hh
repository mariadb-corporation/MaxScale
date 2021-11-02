/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/pcre2.h>
#include <maxscale/utils.hh>

#include <string>

namespace maxscale
{


/**
 * Overload that returns a string
 *
 * @param re Compiled pattern to use
 * @param subject Subject string
 * @param replace Replacement string
 * @param error   Pointer to std::string where any error messages are stored
 *
 * @return The replaced string or the original string if no replacement was made. Returns an empty string when
 * any PCRE2 error is encountered.
 */
std::string pcre2_substitute(pcre2_code* re,
                             const std::string& subject,
                             const std::string& replace,
                             std::string* error = nullptr);

/**
 * @class CloserTraits<pcre2_code*> pcre2.hh <maxscale/pcre2.hh>
 *
 * Specialization of @c CloserTraits for @c pcre2_code*.
 */
template<>
struct CloserTraits<pcre2_code*>
{
    static void close_if(pcre2_code* pCode)
    {
        if (pCode)
        {
            pcre2_code_free(pCode);
        }
    }

    static void reset(pcre2_code*& pCode)
    {
        pCode = NULL;
    }
};

/**
 * @class CloserTraits<pcre2_match_data*> pcre2.hh <maxscale/pcre2.hh>
 *
 * Specialization of @c CloserTraits for @c pcre2_match_data*.
 */
template<>
struct CloserTraits<pcre2_match_data*>
{
    static void close_if(pcre2_match_data* pData)
    {
        if (pData)
        {
            pcre2_match_data_free(pData);
        }
    }

    static void reset(pcre2_match_data*& pData)
    {
        pData = NULL;
    }
};
}

namespace std
{

template<>
class default_delete<pcre2_code>
{
public:
    void operator()(pcre2_code* p)
    {
        if (p)
        {
            pcre2_code_free(p);
        }
    }
};
}
