#pragma once
#ifndef _MAXSCALE_POLL_H
#define _MAXSCALE_POLL_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file poll.h     The poll related functionality
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 19/06/13     Mark Riddoch    Initial implementation
 * 17/10/15     Martin Brampton Declare fake event functions
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <maxscale/dcb.h>
#include <maxscale/gwbitmask.h>
#include <maxscale/resultset.h>
#include <sys/epoll.h>

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
    POLL_STAT_EVQ_PENDING,
    POLL_STAT_EVQ_MAX,
    POLL_STAT_MAX_QTIME,
    POLL_STAT_MAX_EXECTIME
} POLL_STAT;

extern  void            poll_init();
extern  int             poll_add_dcb(DCB *);
extern  int             poll_remove_dcb(DCB *);
extern  void            poll_waitevents(void *);
extern  void            poll_shutdown();
extern  GWBITMASK       *poll_bitmask();
extern  void            poll_set_maxwait(unsigned int);
extern  void            poll_set_nonblocking_polls(unsigned int);
extern  void            dprintPollStats(DCB *);
extern  void            dShowThreads(DCB *dcb);
extern  void            poll_add_epollin_event_to_dcb(DCB* dcb, GWBUF* buf);
extern  void            dShowEventQ(DCB *dcb);
extern  void            dShowEventStats(DCB *dcb);
extern  int             poll_get_stat(POLL_STAT stat);
extern  RESULTSET       *eventTimesGetList();
extern  void            poll_fake_event(DCB *dcb, enum EPOLL_EVENTS ev);
extern  void            poll_fake_hangup_event(DCB *dcb);
extern  void            poll_fake_write_event(DCB *dcb);
extern  void            poll_fake_read_event(DCB *dcb);

MXS_END_DECLS

#endif
