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

#include <maxscale/semaphore.hh>
#include <time.h>

namespace maxscale
{

bool Semaphore::timedwait(time_t seconds,
                          long nseconds,
                          signal_approach_t signal_approach) const
{
    ss_dassert(nseconds <= 999999999);

    timespec ts;

    ss_debug(int rc=) clock_gettime(CLOCK_REALTIME, &ts);
    ss_dassert(rc == 0);

    ts.tv_sec += seconds;

    uint64_t nseconds_sum = ts.tv_nsec + nseconds;

    if (nseconds_sum > 1000000000)
    {
        ts.tv_sec += 1;
        nseconds_sum -= 1000000000;
    }

    ts.tv_nsec = nseconds_sum;

    return timedwait(ts, signal_approach);
}

}
