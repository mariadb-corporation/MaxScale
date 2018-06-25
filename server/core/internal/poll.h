#pragma once
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

/**
 * @file core/maxscale/poll.h - The private poll interface
 */

#include <maxscale/poll.h>

#include <maxscale/resultset.h>

MXS_BEGIN_DECLS

struct mxs_worker;

#define MAX_EVENTS 1000

enum poll_message
{
    POLL_MSG_CLEAN_PERSISTENT = 0x01
};

void            poll_init();
//void          poll_finish(); // TODO: Add this.

void            poll_set_maxwait(unsigned int);
void            poll_set_nonblocking_polls(unsigned int);

void            dprintPollStats(DCB *);
void            dShowThreads(DCB *dcb);
void            dShowEventQ(DCB *dcb);
void            dShowEventStats(DCB *dcb);

RESULTSET       *eventTimesGetList();

MXS_END_DECLS
