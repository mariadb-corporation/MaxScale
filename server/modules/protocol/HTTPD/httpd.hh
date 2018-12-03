#pragma once

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <maxscale/dcb.h>
#include <maxscale/buffer.hh>
#include <maxscale/service.hh>
#include <maxscale/session.hh>
#include <maxscale/router.hh>
#include <maxscale/poll.h>
#include <maxbase/atomic.h>

MXS_BEGIN_DECLS

#define HTTPD_SMALL_BUFFER       1024
#define HTTPD_METHOD_MAXLEN      128
#define HTTPD_USER_MAXLEN        128
#define HTTPD_HOSTNAME_MAXLEN    512
#define HTTPD_USERAGENT_MAXLEN   1024
#define HTTPD_FIELD_MAXLEN       8192
#define HTTPD_REQUESTLINE_MAXLEN 8192

/**
 * HTTPD session specific data
 *
 */
typedef struct httpd_session
{
    char  user[HTTPD_USER_MAXLEN];          /*< username for authentication*/
    char* cookies;                          /*< all input cookies */
    char  hostname[HTTPD_HOSTNAME_MAXLEN];  /*< The hostname */
    char  useragent[HTTPD_USERAGENT_MAXLEN];/*< The useragent */
    char  method[HTTPD_METHOD_MAXLEN];      /*< The HTTPD Method */
    char* url;                              /*< the URL in the request */
    char* path_info;                        /*< the Pathinfo, starts with /, is the extra path segments after
                                             * the document name */
    char* query_string;                     /*< the Query string, starts with ?, after path_info and document
                                             * name */
    int headers_received;                   /*< All the headers has been received, if 1 */
} HTTPD_session;

MXS_END_DECLS
