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
#include <gwbitmask.h>

struct session;
struct server;
struct service;

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
 * 01/06/2013	Mark Riddoch		Initial implementation
 * 11/06/2013	Mark Riddoch		Updated GWPROTOCOL structure with new
 *					entry points
 * 18/06/2013	Mark Riddoch		Addition of the listener entry point
 * 02/07/2013	Massimiliano Pinto	Addition of delayqlock, delayq and authlock
 *					for handling backend asynchronous protocol connection
 *					and a generic lock for backend authentication
 * 12/07/2013	Massimiliano Pinto	Added auth entry point
 * 15/07/2013	Massimiliano Pinto	Added session entry point
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
	 *	auth		Authentication entry point
         *	session		Session handling entry point
	 * @endverbatim
	 *
	 * This forms the "module object" for protocol modules within the gateway.
	 *
	 * @see load_module
         */                             
typedef struct gw_protocol {
	int		(*read)(struct dcb *);
	int		(*write)(struct dcb *, GWBUF *);
	int		(*write_ready)(struct dcb *);
	int		(*error)(struct dcb *);
	int		(*hangup)(struct dcb *);
	int		(*accept)(struct dcb *);
	int		(*connect)(struct dcb *, struct server *, struct session *);
	int		(*close)(struct dcb *);
	int		(*listen)(struct dcb *, char *);
	int		(*auth)(struct dcb *, struct server *, struct session *, GWBUF *);
	int		(*session)(struct dcb *, void *);
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
 * The data structure that is embedded witin a DCB and manages the complex memory
 * management issues of a DCB.
 *
 * The DCB structures are used as the user data within the polling loop. This means that
 * polling threads may aschronously wake up and access these structures. It is not possible
 * to simple remove the DCB from the epoll system and then free the data, as every thread
 * that is currently running an epoll call must wake up and re-issue the epoll_wait system
 * call, the is the only way we can be sure that no polling thread is pending a wakeup or
 * processing an event that will access the DCB.
 *
 * We solve this issue by making the dcb_free routine merely mark a DCB as a zombie and
 * place it on a special zombie list. Before placing the DCB on the zombie list we create
 * a bitmask with a bit set in it for each active polling thread. Each thread will call
 * a routine to process the zombie list at the end of the polling loop. This routine
 * will clear the bit value that corresponds to the calling thread. Once the bitmask
 * is completely cleared the DCB can finally be freed and removed from the zombie list.
 */
typedef struct {
	GWBITMASK	bitmask;	/**< The bitmask of threads */
	struct dcb	*next;		/**< Next pointer for the zombie list */
} DCBMM;

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
	char		*remote;	/**< Address of remote end */
	void		*protocol;	/**< The protocol specific state */
	struct session	*session;	/**< The owning session */
	GWPROTOCOL	func;		/**< The functions for this descrioptor */

	SPINLOCK	writeqlock;	/**< Write Queue spinlock */
	GWBUF		*writeq;	/**< Write Data Queue */
	SPINLOCK	delayqlock;	/**< Delay Backend Write Queue spinlock */
	GWBUF		*delayq;	/**< Delay Backend Write Data Queue */
	SPINLOCK	authlock;	/**< Generic Authorization spinlock */

	DCBSTATS	stats;		/**< DCB related statistics */

	struct dcb	*next;		/**< Next DCB in the chain of allocated DCB's */
	struct service	*service;	/**< The related service */
	void		*data;		/**< Specific client data */
	DCBMM		memdata;	/**< The data related to DCB memory management */
} DCB;

/* DCB states */
#define	DCB_STATE_ALLOC		0	/**< Memory allocated but not populated */
#define DCB_STATE_IDLE		1	/**< Not yet in the poll mask */
#define DCB_STATE_POLLING	2	/**< Waiting in the poll loop */
#define DCB_STATE_PROCESSING	4	/**< Processing an event */
#define DCB_STATE_LISTENING	5	/**< The DCB is for a listening socket */
#define DCB_STATE_DISCONNECTED	6	/**< The socket is now closed */
#define DCB_STATE_FREED		7	/**< Memory freed */
#define DCB_STATE_ZOMBIE	8	/**< DCB is no longer active, waiting to free it */

/* A few useful macros */
#define	DCB_SESSION(x)			(x)->session
#define DCB_PROTOCOL(x, type)		(type *)((x)->protocol)
#define	DCB_ISZOMBIE(x)			((x)->state == DCB_STATE_ZOMBIE)

extern DCB		*dcb_alloc();				/* Allocate a DCB */
extern void		dcb_free(DCB *);			/* Free a DCB */
extern DCB		*dcb_connect(struct server *, struct session *, const char *);	/* prepare Backend connection */
extern int		dcb_read(DCB *, GWBUF **);		/* Generic read routine */
extern int		dcb_write(DCB *, GWBUF *);		/* Generic write routine */
extern int		dcb_drain_writeq(DCB *);		/* Generic write routine */
extern void		dcb_close(DCB *);			/* Generic close functionality */
extern void		dcb_process_zombies(int);		/* Process Zombies */
extern void		printAllDCBs();				/* Debug to print all DCB in the system */
extern void		printDCB(DCB *);			/* Debug print routine */
extern void		dprintAllDCBs(DCB *);			/* Debug to print all DCB in the system */
extern void		dprintDCB(DCB *, DCB *);		/* Debug to print a DCB in the system */
extern const char 	*gw_dcb_state2string(int);		/* DCB state to string */
extern void		dcb_printf(DCB *, const char *, ...);	/* DCB version of printf */
extern int		dcb_isclient(DCB *);			/* the DCB is the client of the session */

#endif
