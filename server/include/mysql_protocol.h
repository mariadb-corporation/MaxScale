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

/*
 * Revision History
 *
 * Date		Who			Description
 * 01-06-2013	Mark Riddoch		Initial implementation
 * 14-06-2013	Massimiliano Pinto	Added specific data
 *					for MySQL session
 */

#ifndef MYSQL_SCRAMBLE_LEN
#define MYSQL_SCRAMBLE_LEN GW_MYSQL_SCRAMBLE_SIZE
#endif

#define MYSQL_USER_MAXLEN 128
#define MYSQL_DATABASE_MAXLEN 128

typedef enum {
    MYSQL_ALLOC,
    MYSQL_AUTH_SENT,
    MYSQL_AUTH_RECV,
    MYSQL_AUTH_FAILED,
    MYSQL_IDLE,
    MYSQL_ROUTING,
    MYSQL_WAITING_RESULT,
} mysql_pstate_t;

struct dcb;

/**
 * MySQL Protocol specific state data
 */
typedef struct {
        skygw_chk_t     protocol_chk_top;
	int		fd;				/**< The socket descriptor */
	struct dcb	*descriptor;			/**< The DCB of the socket we are running on */
	mysql_pstate_t	state;				/**< Current descriptor state */
	char		scramble[MYSQL_SCRAMBLE_LEN];	/**< server scramble, created or received */
	uint32_t	server_capabilities;		/**< server capabilities, created or received */
	uint32_t	client_capabilities;		/**< client capabilities, created or received */
	unsigned	long tid;			/**< MySQL Thread ID, in handshake */
        skygw_chk_t     protocol_chk_tail;
} MySQLProtocol;

/**
 * MySQL session specific data
 *
 */
typedef struct mysql_session {
	uint8_t client_sha1[MYSQL_SCRAMBLE_LEN];	/**< SHA1(passowrd) */
	char user[MYSQL_USER_MAXLEN];			/**< username */
	char db[MYSQL_DATABASE_MAXLEN];			/**< database */
} MYSQL_session;

#endif
