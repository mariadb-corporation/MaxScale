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

/**
 * @file poll_basic.h  The Descriptor Control Block
 */

#include <maxscale/cdefs.h>
#include <sys/epoll.h>
#include <maxscale/atomic.h>

MXS_BEGIN_DECLS

typedef enum mxs_poll_action
{
    MXS_POLL_NOP    = 0x00,
    MXS_POLL_ACCEPT = 0x01,
    MXS_POLL_READ   = 0x02,
    MXS_POLL_WRITE  = 0x04,
    MXS_POLL_HUP    = 0x08,
    MXS_POLL_ERROR  = 0x10,
} mxs_poll_action_t;

struct mxs_poll_data;

/**
 * Pointer to function that knows how to handle events for a particular
 * 'struct mxs_poll_data' structure.
 *
 * @param data    The `mxs_poll_data` instance that contained this function pointer.
 * @param worker  The worker.
 * @param events  The epoll events.
 *
 * @return A combination of mxs_poll_action_t enumeration values.
 */
// TODO: Change worker to mxs::Worker once this is C++-ified.
typedef uint32_t (*mxs_poll_handler_t)(struct mxs_poll_data* data, void* worker, uint32_t events);

typedef struct mxs_poll_data
{
    mxs_poll_handler_t handler; /*< Handler for this particular kind of mxs_poll_data. */
    void*              owner;   /*< Owning worker. */
} MXS_POLL_DATA;

MXS_END_DECLS
