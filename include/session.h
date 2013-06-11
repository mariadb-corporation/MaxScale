#ifndef _SESSION_H
#define _SESSION_H
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

struct dcb;

/*
 * The session status block
 */
typedef struct session {
	int 		state;		/* Current descriptor state */
	struct dcb	*client;	/* The client connection */
	struct dcb	*backends;	/* The set of backend servers */
} SESSION;

#define SESSION_STATE_ALLOC		0
#define SESSION_STATE_READY		1

#define SESSION_PROTOCOL(x, type)	DCB_PROTOCOL((x)->client, type)
#endif
