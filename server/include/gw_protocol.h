#ifndef GW_PROTOCOL_H
#define GW_PROTOCOL_H
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
 *
 * @endverbatim
 */

#include <buffer.h>

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
} GWPROTOCOL;

/**
 * The GWPROTOCOL version data. The following should be updated whenever
 * the GWPROTOCOL structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define GWPROTOCOL_VERSION      {1, 0, 0}


#endif /* GW_PROTOCOL_H */

