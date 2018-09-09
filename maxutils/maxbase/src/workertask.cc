/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxbase/workertask.hh>
#include <maxbase/atomic.h>
#include <maxbase/assert.h>

namespace maxbase
{

//
// WorkerTask
//
WorkerTask::~WorkerTask()
{
}

//
// WorkerDisposableTask
//
WorkerDisposableTask::WorkerDisposableTask()
    : m_count(0)
{
}

WorkerDisposableTask::~WorkerDisposableTask()
{
}

void WorkerDisposableTask::inc_ref()
{
    atomic_add(&m_count, 1);
}

void WorkerDisposableTask::dec_ref()
{
    mxb_assert(atomic_load_int32(&m_count) > 0);

    if (atomic_add(&m_count, -1) == 1)
    {
        delete this;
    }
}
}
