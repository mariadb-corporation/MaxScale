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

/*
 * Revision History
 *
 * Date		Who			Description
 * 08-07-2013	Massimiliano Pinto	Added HTTPD protocol header file 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dcb.h>
#include <buffer.h>
#include <service.h>
#include <session.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <router.h>
#include <maxscale/poll.h>
#include <atomic.h>
#include <gw.h>

#define HTTPD_SMALL_BUFFER 1024
#define HTTPD_METHOD_MAXLEN 128
#define HTTPD_USER_MAXLEN 128
#define HTTPD_HOSTNAME_MAXLEN 512
#define HTTPD_USERAGENT_MAXLEN 1024
#define HTTPD_FIELD_MAXLEN 8192
#define HTTPD_REQUESTLINE_MAXLEN 8192

/**
 * HTTPD session specific data
 *
 */
typedef struct httpd_session {
        char user[HTTPD_USER_MAXLEN];			/*< username for authentication*/
        char *cookies;					/*< all input cookies */
        char hostname[HTTPD_HOSTNAME_MAXLEN];		/*< The hostname */
        char useragent[HTTPD_USERAGENT_MAXLEN];		/*< The useragent */
        char method[HTTPD_METHOD_MAXLEN];		/*< The HTTPD Method */
	char *url;					/*< the URL in the request */
	char *path_info;				/*< the Pathinfo, starts with /, is the extra path segments after the document name */
	char *query_string;				/*< the Query string, starts with ?, after path_info and document name */
	int headers_received;				/*< All the headers has been received, if 1 */
} HTTPD_session;
