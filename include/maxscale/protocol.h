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
 * @file protocol.h
 *
 * The protocol module interface definition.
 */

#include <maxscale/cdefs.h>
#include <maxscale/buffer.h>

MXS_BEGIN_DECLS

struct dcb;
struct server;
struct session;

/**
 * @verbatim
 * The operations that can be performed on the descriptor
 *
 *      read            EPOLLIN handler for the socket
 *      write           MaxScale data write entry point
 *      write_ready     EPOLLOUT handler for the socket, indicates
 *                      that the socket is ready to send more data
 *      error           EPOLLERR handler for the socket
 *      hangup          EPOLLHUP handler for the socket
 *      accept          Accept handler for listener socket only
 *      connect         Create a connection to the specified server
 *                      for the session pased in
 *      close           MaxScale close entry point for the socket
 *      listen          Create a listener for the protocol
 *      auth            Authentication entry point
 *      session         Session handling entry point
 *      auth_default    Default authenticator name
 *      connlimit       Maximum connection limit
 *      established     Whether connection is fully established
 * @endverbatim
 *
 * This forms the "module object" for protocol modules within the gateway.
 *
 * @see load_module
 */
typedef struct mxs_protocol
{
    int32_t (*read)(struct dcb *);
    int32_t (*write)(struct dcb *, GWBUF *);
    int32_t (*write_ready)(struct dcb *);
    int32_t (*error)(struct dcb *);
    int32_t (*hangup)(struct dcb *);
    int32_t (*accept)(struct dcb *);
    int32_t (*connect)(struct dcb *, struct server *, struct session *);
    int32_t (*close)(struct dcb *);
    int32_t (*listen)(struct dcb *, char *);
    int32_t (*auth)(struct dcb *, struct server *, struct session *, GWBUF *);
    int32_t (*session)(struct dcb *, void *); // TODO: remove this, not used
    char   *(*auth_default)();
    int32_t (*connlimit)(struct dcb *, int limit);
    bool    (*established)(struct dcb *);
} MXS_PROTOCOL;

/**
 * The MXS_PROTOCOL version data. The following should be updated whenever
 * the MXS_PROTOCOL structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_PROTOCOL_VERSION      {1, 1, 0}

MXS_END_DECLS
