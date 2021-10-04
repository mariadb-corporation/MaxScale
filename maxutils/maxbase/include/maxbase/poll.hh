/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>

namespace maxbase
{

namespace poll_action
{
constexpr uint32_t NOP = 0x00;
constexpr uint32_t ACCEPT = 0x01;
constexpr uint32_t READ = 0x02;
constexpr uint32_t WRITE = 0x04;
constexpr uint32_t HUP = 0x08;
constexpr uint32_t ERROR = 0x10;
}

struct POLL_DATA;
struct WORKER
{
};
/**
 * Pointer to function that knows how to handle events for a particular
 * MXB_POLL_DATA structure.
 *
 * @param data    The MXB_POLL_DATA instance that contained this function pointer.
 * @param worker  The worker.
 * @param events  The epoll events.
 *
 * @return A combination of mxb_poll_action_t enumeration values.
 */
using poll_handler_t = uint32_t (*)(POLL_DATA* data, WORKER* worker, uint32_t events);

struct POLL_DATA
{
    poll_handler_t handler; /*< Handler for this particular kind of mxb_poll_data. */
    WORKER*        owner;   /*< Owning worker. */
};

class PollData : public POLL_DATA
{
public:
    PollData()
    {
        handler = NULL;
        owner = nullptr;
    }

    PollData(poll_handler_t h)
    {
        handler = h;
        owner = nullptr;
    }
};
}
