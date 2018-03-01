#pragma once
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

/**
 * @file core/maxscale/poll.h - The private poll interface
 */

#include <maxscale/poll.h>

#include <maxscale/resultset.h>

MXS_BEGIN_DECLS

#define MAX_EVENTS 1000

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
    POLL_STAT_EVQ_LEN,
    POLL_STAT_EVQ_MAX,
    POLL_STAT_MAX_QTIME,
    POLL_STAT_MAX_EXECTIME
} POLL_STAT;

enum poll_message
{
    POLL_MSG_CLEAN_PERSISTENT = 0x01
};

void            poll_init();
void            poll_shutdown();

void            poll_waitevents(void *);
void            poll_set_maxwait(unsigned int);
void            poll_set_nonblocking_polls(unsigned int);

void            dprintPollStats(DCB *);
void            dShowThreads(DCB *dcb);
void            dShowEventQ(DCB *dcb);
void            dShowEventStats(DCB *dcb);

int64_t         poll_get_stat(POLL_STAT stat);
RESULTSET       *eventTimesGetList();

void            poll_send_message(enum poll_message msg, void *data);

MXS_END_DECLS
