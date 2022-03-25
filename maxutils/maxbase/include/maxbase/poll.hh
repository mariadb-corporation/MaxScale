/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
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

struct PollData;
class Worker;

/**
 * Pointer to function that knows how to handle events for a particular
 * PollData structure.
 *
 * @param data    The PollData instance that contained this function pointer.
 * @param worker  The worker.
 * @param events  The epoll events.
 *
 * @return A combination of mxb_poll_action_t enumeration values.
 */
using poll_handler_t = uint32_t (*)(PollData* data, Worker* worker, uint32_t events);

class PollData
{
public:
    PollData()
    {
    }

    PollData(poll_handler_t h)
        : handler(h)
    {
    }

    PollData(poll_handler_t h, Worker* o)
        : handler(h)
        , owner(o)
    {
    }

    poll_handler_t handler { nullptr} ; /*< Handler for this particular kind of mxb_poll_data. */
    Worker*        owner { nullptr };   /*< Owning worker. */
};

}
