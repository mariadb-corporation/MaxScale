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
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/pcre2.h>
#include <maxscale/utils.hh>

namespace maxscale
{

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
