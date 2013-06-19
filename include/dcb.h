#ifndef _DCB_H
#define _DCB_H
/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */
#include <spinlock.h>
#include <buffer.h>

struct session;
struct server;

/**
 * @file dcb.h	The Descriptor Control Block
 *
 * The function pointer table used by descriptors to call relevant functions
 * within the protocol specific code.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 01/06/13	Mark Riddoch		Initial implementation
 * 11/06/13	Mark Riddoch		Updated GWPROTOCOL structure with new
 *					entry points
 * 18/06/13	Mark Riddoch		Addition of the listener entry point
 *
 * @endverbatim
 */

struct dcb;

        /**
	 * @verbatim
         * The operations that can be performed on the descriptor
	 *
	 *	read		EPOLLIN handler for the socket
	 *	write		Gateway data write entry point
	 *	write_ready	EPOLLOUT handler for the socket, indicates
	 *			that the socket is ready to send more data
	 *	error		EPOLLERR handler for the socket
	 *	hangup		EPOLLHUP handler for the socket
	 *	accept		Accept handler for listener socket only
	 *	connect		Create a connection to the specified server
	 *			for the session pased in
	 *	close		Gateway close entry point for the socket
	 *	listen		Create a listener for the protocol
	 * @endverbatim
	 *
	 * This forms the "module object" for protocol modules within the gateway.
	 *
	 * @see load_module
         */                             
typedef struct gw_protocol {
	int		(*read)(struct dcb *, int);
	int		(*write)(struct dcb *, GWBUF *);
	int		(*write_ready)(struct dcb *, int);
	int		(*error)(struct dcb *, int);
	int		(*hangup)(struct dcb *, int);
	int		(*accept)(struct dcb *, int);
	int		(*connect)(struct server *, struct session *, int);
	int		(*close)(struct dcb *, int);
	int		(*listen)(struct dcb *, int, char *);
} GWPROTOCOL;

/**
 * The statitics gathered on a descriptor control block
 */
typedef struct dcbstats {
	int		n_reads;	/**< Number of reads on this descriptor */
	int		n_writes;	/**< Number of writes on this descriptor */
	int		n_accepts;	/**< Number of accepts on this descriptor */
	int		n_buffered;	/**< Number of buffered writes */
} DCBSTATS;

/**
 * Descriptor Control Block
 *
 * A wrapper for a network descriptor within the gateway, it contains all the
 * state information necessary to allow for the implementation of the asynchronous
 * operation of the potocol and gateway functions. It also provides links to the service
 * and session data that is required to route the information within the gateway.
 *
 * It is important to hold the state information here such that any thread within the
 * gateway may be selected to execute the required actions when a network event occurs.
 */
typedef struct dcb {
	int		fd;		/**< The descriptor */
	int 		state;		/**< Current descriptor state */
	void		*protocol;	/**< The protocol specific state */
	struct session	*session;	/**< The owning session */
	GWPROTOCOL	func;		/**< The functions for this descrioptor */

	SPINLOCK	writeqlock;	/**< Write Queue spinlock */
	GWBUF		*writeq;	/**< Write Data Queue */

	DCBSTATS	stats;		/**< DCB related statistics */

	struct dcb	*next;		/**< Next DCB in the chain of allocated DCB's */
} DCB;

/* DCB states */
#define	DCB_STATE_ALLOC		0	/**< Memory allocated but not populated */
#define DCB_STATE_IDLE		1	/**< Not yet in the poll mask */
#define DCB_STATE_POLLING	2	/**< Waiting in the poll loop */
#define DCB_STATE_PROCESSING	4	/**< Processing an event */
#define DCB_STATE_LISTENING	5	/**< The DCB is for a listening socket */
#define DCB_STATE_DISCONNECTED	6	/**< The socket is now closed */
#define DCB_STATE_FREED		7	/**< Memory freed */

/* A few useful macros */
#define	DCB_SESSION(x)			(x)->session
#define DCB_PROTOCOL(x, type)		(type *)((x)->protocol)

extern DCB		*alloc_dcb();			/* Allocate a DCB */
extern void		free_dcb(DCB *);		/* Free a DCB */
extern DCB		*connect_dcb(struct server *, struct session *, const char *);
extern int		dcb_read(DCB *, GWBUF **);	/* Generic read routine */
extern int		dcb_write(DCB *, GWBUF *);	/* Generic write routine */
extern int		dcb_drain_writeq(DCB *);	/* Generic write routine */
extern void		dcb_close(DCB *, int);		/* Generic close functionality */
extern void		printAllDCBs();			/* Debug to print all DCB in the system */
extern void		printDCB(DCB *);		/* Debug print routine */
extern void		dprintAllDCBs(DCB *);		/* Debug to print all DCB in the system */
extern const char 	*gw_dcb_state2string(int);	/* DCB state to string */
extern void		dcb_printf(DCB *, const char *, ...);	/* DCB version of printf */

#endif
