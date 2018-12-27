/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/mainworker.hh>
#include <signal.h>

namespace
{

static struct ThisUnit
{
    maxscale::MainWorker* pThis;
} this_unit;

}

namespace maxscale
{

MainWorker::MainWorker()
{
    mxb_assert(!this_unit.pThis);

    this_unit.pThis = this;
}

MainWorker::~MainWorker()
{
    mxb_assert(this_unit.pThis);

    this_unit.pThis = nullptr;
}

//static
MainWorker& MainWorker::get()
{
    mxb_assert(this_unit.pThis);

    return *this_unit.pThis;
}

bool MainWorker::pre_run()
{
    return true;
}

void MainWorker::post_run()
{
}

void MainWorker::epoll_tick()
{
}

}
