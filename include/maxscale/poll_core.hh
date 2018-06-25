#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/poll_core.h>

namespace maxscale
{

struct MxsPollData : MXS_POLL_DATA
{
    MxsPollData()
    {
        handler = NULL;
        thread.id = 0;
    }

    MxsPollData(mxs_poll_handler_t h)
    {
        handler = h;
        thread.id = 0;
    }
};

}
