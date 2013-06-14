#ifndef _MYSQL_PROTOCOL_H
#define _MYSQL_PROTOCOL_H
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

#ifndef MYSQL_SCRAMBLE_LEN
#define MYSQL_SCRAMBLE_LEN GW_MYSQL_SCRAMBLE_SIZE
#endif

#define MYSQL_USER_MAXLEN 128
#define MYSQL_DATABASE_MAXLEN 128

struct dcb;

/*
 * MySQL Protocol specific state data
 */
typedef struct {
	int		fd;				/ * The socket descriptor */
	struct dcb	*descriptor;			/* The DCB of the socket we are running on */
	int		state;				/* Current descriptor state */
	char		scramble[MYSQL_SCRAMBLE_LEN];	/* server scramble, created or received */
	uint32_t	server_capabilities;		/* server capabilities, created or received */
	uint32_t	client_capabilities;		/* client capabilities, created or received */
	unsigned	long tid;			/* MySQL Thread ID, in handshake */
} MySQLProtocol;

/*
 * MySQL session specific data
 *
 */
typedef struct mysql_session {
	uint8_t client_sha1[MYSQL_SCRAMBLE_LEN];	/* SHA1(passowrd) */
	char user[MYSQL_USER_MAXLEN];			/* username */
	char db[MYSQL_DATABASE_MAXLEN];			/* database */
} MYSQL_session;

/* MySQL Protocol States */
#define MYSQL_ALLOC		0	/* Allocate data */
#define MYSQL_AUTH_SENT		1	/* Authentication handshake has been sent */
#define MYSQL_AUTH_RECV		2	/* Received user, password, db and capabilities */
#define MYSQL_AUTH_FAILED	3	/* Auth failed, return error packet */
#define MYSQL_IDLE		4	/* Auth done. Protocol is idle, waiting for statements */
#define MYSQL_ROUTING		5	/* The received command has been routed to backend(s) */
#define MYSQL_WAITING_RESULT	6	/* Waiting for result set */

#endif
