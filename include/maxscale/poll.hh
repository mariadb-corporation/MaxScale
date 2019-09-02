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
#pragma once

/**
 * @file include/maxscale/poll.hh - The public poll interface
 */

#include <maxscale/ccdefs.hh>

#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>

MXS_BEGIN_DECLS

/**
 * A statistic identifier that can be returned by poll_get_stat
 */
typedef enum
{
    POLL_STAT_READ,
    POLL_STAT_WRITE,
    POLL_STAT_ERROR,
    POLL_STAT_HANGUP,
    POLL_STAT_ACCEPT,
    POLL_STAT_EVQ_AVG,
    POLL_STAT_EVQ_MAX,
    POLL_STAT_MAX_QTIME,
    POLL_STAT_MAX_EXECTIME
} POLL_STAT;

/*
 * Return a particular statistics value.
 *
 * @param stat  What to return.
 *
 * @return The value.
 */
int64_t poll_get_stat(POLL_STAT stat);

/**
 * Add given GWBUF to DCB's readqueue and add a pending EPOLLIN event for DCB.
 * The event pretends that there is something to read for the DCB. Actually
 * the incoming data is stored in the DCB's readqueue where it is read.
 *
 * @param dcb   DCB where the event and data are added
 * @param buf   GWBUF including the data
 *
 */
void poll_add_epollin_event_to_dcb(DCB* dcb, GWBUF* buf);

MXS_END_DECLS
