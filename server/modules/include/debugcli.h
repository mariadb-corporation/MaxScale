#ifndef _DEBUGCLI_H
#define _DEBUGCLI_H
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
#include <service.h>
#include <session.h>
#include <spinlock.h>

/**
 * @file debugcli.h The debug interface to the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 18/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
struct cli_session;

/**
 * The CLI_INSTANCE structure. There is one instane of the CLI "router" for
 * each service that uses the CLI.
 */
typedef struct cli_instance {
	SPINLOCK	lock;		/*< The instance spinlock */
	SERVICE		*service;	/*< The debug cli service */
	int		mode;		/*< CLI interface mode */
	struct cli_session
			*sessions;	/*< Linked list of sessions within this instance */
	struct cli_instance
			*next;		/*< The next pointer for the list of instances */
} CLI_INSTANCE;

/**
 * The CLI_SESSION structure. As CLI_SESSION is created for each user that logs into
 * the DEBUG CLI.
 */
enum { cmdbuflen=80 };

typedef struct cli_session {
	char		cmdbuf[cmdbuflen]; /*< The command buffer used to build up user commands */
	int		mode;		   /*< The CLI Mode for this session */
	SESSION		*session;	   /*< The gateway session */
	struct cli_session
			*next;		   /*< The next pointer for the list of sessions */
} CLI_SESSION;

/* Command line interface modes */
#define	CLIM_USER		1
#define CLIM_DEVELOPER		2
#endif
