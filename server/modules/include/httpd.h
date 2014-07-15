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
#include <poll.h>
#include <atomic.h>
#include <gw.h>

#define HTTPD_SMALL_BUFFER 1024
#define HTTPD_METHOD_MAXLEN 128
#define HTTPD_USER_MAXLEN 128
#define HTTPD_HOSTNAME_MAXLEN 512
#define HTTPD_USERAGENT_MAXLEN 1024
#define HTTPD_FIELD_MAXLEN 8192
#define HTTPD_REQUESTLINE_MAXLEN 8192

typedef enum {
	METHOD_UNKNOWN = 0,
	METHOD_POST,
	METHOD_PUT,
	METHOD_GET,
	METHOD_HEAD
} HTTP_METHOD;
/**
 * HTTPD session specific data
 *
 */
typedef struct httpd_session {
	HTTP_METHOD		method;
	GWBUF			*saved;
	int			request_len;
	char 			*url;
} HTTPD_session;
