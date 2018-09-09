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
#pragma once

/**
 * @file include/maxscale/poll.h - The public poll interface
 */

#include <maxscale/cdefs.h>

#include <maxscale/buffer.h>
#include <maxscale/dcb.h>

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

/*
 * Insert a fake hangup event for a DCB into the polling queue.
 *
 * This is used when a monitor detects that a server is not responding.
 *
 * @param dcb   DCB to emulate an EPOLLOUT event for
 */
void poll_fake_hangup_event(DCB* dcb);

/*
 * Insert a fake write completion event for a DCB into the polling
 * queue.
 *
 * This is used to trigger transmission activity on another DCB from
 * within the event processing routine of a DCB. or to allow a DCB
 * to defer some further output processing, to allow for other DCBs
 * to receive a slice of the processing time. Fake events are added
 * to the tail of the event queue, in the same way that real events
 * are, so maintain the "fairness" of processing.
 *
 * @param dcb   DCB to emulate an EPOLLOUT event for
 */
void poll_fake_write_event(DCB* dcb);

/*
 * Insert a fake read completion event for a DCB into the polling
 * queue.
 *
 * This is used to trigger transmission activity on another DCB from
 * within the event processing routine of a DCB. or to allow a DCB
 * to defer some further input processing, to allow for other DCBs
 * to receive a slice of the processing time. Fake events are added
 * to the tail of the event queue, in the same way that real events
 * are, so maintain the "fairness" of processing.
 *
 * @param dcb   DCB to emulate an EPOLLIN event for
 */
void poll_fake_read_event(DCB* dcb);

/**
 * Add a DCB to the set of descriptors within the polling
 * environment.
 *
 * @param dcb   The descriptor to add to the poll
 * @return      -1 on error or 0 on success
 */
int poll_add_dcb(DCB*);

/**
 * Remove a descriptor from the set of descriptors within the
 * polling environment.
 *
 * @param dcb   The descriptor to remove
 * @return      -1 on error or 0 on success; actually always 0
 */
int poll_remove_dcb(DCB*);

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
