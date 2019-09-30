/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxscale/maxscaleworker.hh>
#include <maxscale/mainworker.hh>

namespace maxscale
{

MaxScaleWorker::MaxScaleWorker(MainWorker* pMain)
    : m_main(*pMain)
    , m_alive(true)
{
    m_main.add(this);
}

MaxScaleWorker::~MaxScaleWorker()
{
    m_main.remove(this);
}

void MaxScaleWorker::epoll_tick()
{
    // TODO: Add watchdog functionality.
    epoll_tock();
}

}
