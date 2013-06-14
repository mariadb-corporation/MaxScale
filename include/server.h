#ifndef _SERVER_H
#define _SERVER_H
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

/*
 * The server level definitions within the gateway
 *
 * Revision History
 *
 * Date		Who		Description
 * 14/06/13	Mark Riddoch	Initial implementation
 *
 */
typedef struct server {
	char		*name;		/* Server name/IP address*/
	int		port;		/* Port to listen on */
	char		*protocol;	/* Protocol module to use */
	struct	server	*next;		/* Next server */
} SERVER;
#endif
