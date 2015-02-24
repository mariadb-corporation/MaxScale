#ifndef _TELNETD_H
#define _TELNETD_H
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
 * @file telnetd.h The telnetd protocol module header file
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 17/07/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <dcb.h>
#include <housekeeper.h>
/**
 * The telnetd specific protocol structure to put in the DCB.
 */
typedef struct	telnetd {
	int	state;		/**< The connection state */
	char	*username;	/**< The login name of the user */
} TELNETD;

#define	TELNETD_STATE_LOGIN	1	/**< Issued login prompt */
#define TELNETD_STATE_PASSWD	2	/**< Issued password prompt */
#define TELNETD_STATE_DATA	3	/**< User logged in */

#define TELNET_SE               240 
#define TELNET_NOP              241 
#define TELNET_DATA_MARK        242 
#define TELNET_BRK              243 
#define TELNET_IP               244 
#define TELNET_AO               245 
#define TELNET_AYT              246 
#define TELNET_EC               247 
#define TELNET_EL               248 
#define TELNET_GA               249 
#define TELNET_SB               250 
#define TELNET_WILL             251 
#define TELNET_WONT             252 
#define TELNET_DO               253 
#define TELNET_DONT             254 
#define TELNET_IAC              255 
#define TELNET_ECHO               1 
#define TELNET_SUPPRESS_GO_AHEAD  3 
#endif
