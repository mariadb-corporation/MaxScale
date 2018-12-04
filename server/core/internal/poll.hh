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
 * The private poll header
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/poll.hh>
#include <maxscale/resultset.hh>

struct mxs_worker;

enum poll_message
{
    POLL_MSG_CLEAN_PERSISTENT = 0x01
};

void poll_init();
// void          poll_finish(); // TODO: Add this.

void poll_set_maxwait(unsigned int);
void poll_set_nonblocking_polls(unsigned int);

void dprintPollStats(DCB*);
void dShowThreads(DCB* dcb);
void dShowEventQ(DCB* dcb);
void dShowEventStats(DCB* dcb);

std::unique_ptr<ResultSet> eventTimesGetList();
