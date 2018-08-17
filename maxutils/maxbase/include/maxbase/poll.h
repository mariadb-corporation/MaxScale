#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/cdefs.h>
#include <sys/epoll.h>

MXB_BEGIN_DECLS

typedef enum mxb_poll_action_t
{
    MXB_POLL_NOP    = 0x00,
    MXB_POLL_ACCEPT = 0x01,
    MXB_POLL_READ   = 0x02,
    MXB_POLL_WRITE  = 0x04,
    MXB_POLL_HUP    = 0x08,
    MXB_POLL_ERROR  = 0x10,
} mxb_poll_action_t;

struct MXB_POLL_DATA;

typedef struct MXB_WORKER
{
} MXB_WORKER;

/**
 * Pointer to function that knows how to handle events for a particular
 * MXB_POLL_DATA structure.
 *
 * @param data    The MXB_POLL_DATA instance that contained this function pointer.
 * @param worker  The worker.
 * @param events  The epoll events.
 *
 * @return A combination of mxb_poll_action_t enumeration values.
 */
typedef uint32_t (*mxb_poll_handler_t)(struct MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);

typedef struct MXB_POLL_DATA
{
    mxb_poll_handler_t handler; /*< Handler for this particular kind of mxb_poll_data. */
    MXB_WORKER*        owner;   /*< Owning worker. */
} MXB_POLL_DATA;

MXB_END_DECLS
