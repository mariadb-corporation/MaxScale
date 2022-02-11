/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/worker.hh>

namespace maxbase
{
class Worker;
}

namespace maxbase
{

/**
 * @brief MeasureTime is a helper class to keep as a member to
 *        measure time for a Worker.
 *
 *        NOTE: The time used in MeasureTime is epoll_tick_now().
 */
class MeasureTime
{
public:
    enum class Operation {NOP, READ, WRITE};

    MeasureTime(mxb::Worker* pWorker)
        : m_pWorker(pWorker)
    {
    }

    void start(Operation opr)
    {
        m_start = m_pWorker->epoll_tick_now();
        m_opr = opr;
    }

    void stop()
    {
        m_stop = m_pWorker->epoll_tick_now();
    }

    Duration duration()
    {
        return m_stop - m_start;
    }

    Operation opr()
    {
        return m_opr;
    }
private:
    mxb::Worker*   m_pWorker;
    Operation            m_opr;
    mxb::TimePoint m_start;
    mxb::TimePoint m_stop;
};
}
