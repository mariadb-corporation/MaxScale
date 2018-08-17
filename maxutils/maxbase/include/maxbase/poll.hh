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

#include <maxbase/ccdefs.hh>
#include <maxbase/poll.h>

namespace maxbase
{

class PollData : public MXB_POLL_DATA
{
public:
    PollData()
    {
        handler = NULL;
        owner = nullptr;
    }

    PollData(mxb_poll_handler_t h)
    {
        handler = h;
        owner = nullptr;
    }
};

}
