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
#include <maxscale/jansson.h>
#include <maxscale/utils.hh>

namespace maxscale
{

/**
 * @class CloserTraits<json_t*> jansson.hh <maxscale/jansson.hh>
 *
 * Specialization of @c CloserTraits for @c json_t*.
 */
template<>
struct CloserTraits<json_t*>
{
    static void close_if(json_t* pJson)
    {
        if (pJson)
        {
            json_decref(pJson);
        }
    }

    static void reset(json_t*& pJson)
    {
        pJson = NULL;
    }
};

}
