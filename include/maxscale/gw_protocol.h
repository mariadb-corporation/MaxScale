#ifndef GW_PROTOCOL_H
#define GW_PROTOCOL_H
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
 * @file protocol.h
 *
 * The listener definitions for MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 22/01/16     Martin Brampton         Initial implementation
 * 31/05/16     Martin Brampton         Add API entry for connection limit
 *
 * @endverbatim
 */

#include <maxscale/buffer.h>

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
 *  session         Session handling entry point
 * @endverbatim
 *
 * This forms the "module object" for protocol modules within the gateway.
 *
 * @see load_module
 */
typedef struct gw_protocol
{
    int (*read)(struct dcb *);
    int (*write)(struct dcb *, GWBUF *);
    int (*write_ready)(struct dcb *);
    int (*error)(struct dcb *);
    int (*hangup)(struct dcb *);
    int (*accept)(struct dcb *);
    int (*connect)(struct dcb *, struct server *, struct session *);
    int (*close)(struct dcb *);
    int (*listen)(struct dcb *, char *);
    int (*auth)(struct dcb *, struct server *, struct session *, GWBUF *);
    int (*session)(struct dcb *, void *);
    char *(*auth_default)();
    int (*connlimit)(struct dcb *, int limit);
} GWPROTOCOL;

/**
 * The GWPROTOCOL version data. The following should be updated whenever
 * the GWPROTOCOL structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define GWPROTOCOL_VERSION      {1, 1, 0}


#endif /* GW_PROTOCOL_H */

