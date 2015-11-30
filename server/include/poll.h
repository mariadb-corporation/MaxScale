#ifndef _POLL_H
#define _POLL_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */
#include <dcb.h>
#include <gwbitmask.h>
#include <resultset.h>

/**
 * @file poll.h The poll related functionality
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 19/06/13     Mark Riddoch    Initial implementation
 * 17/10/15 Martin Brampton Declare fake event functions
 *
 * @endverbatim
 */
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
    POLL_STAT_EVQ_PENDING,
    POLL_STAT_EVQ_MAX,
    POLL_STAT_MAX_QTIME,
    POLL_STAT_MAX_EXECTIME
} POLL_STAT;

extern  void poll_init();
extern  int poll_add_dcb(DCB *);
extern  int poll_remove_dcb(DCB *);
extern  void poll_waitevents(void *);
extern  void poll_shutdown();
extern  GWBITMASK* poll_bitmask();
extern  void poll_set_maxwait(unsigned int);
extern  void poll_set_nonblocking_polls(unsigned int);
extern  void dprintPollStats(DCB *);
extern  void dShowThreads(DCB *dcb);
void poll_add_epollin_event_to_dcb(DCB* dcb, GWBUF* buf);
extern  void dShowEventQ(DCB *dcb);
extern  void dShowEventStats(DCB *dcb);
extern  int poll_get_stat(POLL_STAT stat);
extern  RESULTSET* eventTimesGetList();
extern  void poll_fake_hangup_event(DCB *dcb);
extern  void poll_fake_write_event(DCB *dcb);

#endif
