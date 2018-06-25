#pragma once
#ifndef _CDC_H
#define _CDC_H
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

/*
 * Revision History
 *
 * Date         Who                 Description
 * 11-01-2016   Massimiliano Pinto  First Implementation
 */

#include <maxscale/cdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/dcb.h>
#include <maxscale/buffer.h>
#include <maxscale/service.h>
#include <maxscale/session.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <maxscale/router.h>
#include <maxscale/poll.h>
#include <maxscale/atomic.h>

MXS_BEGIN_DECLS

#define CDC_SMALL_BUFFER       1024
#define CDC_METHOD_MAXLEN      128
#define CDC_USER_MAXLEN        128
#define CDC_HOSTNAME_MAXLEN    512
#define CDC_USERAGENT_MAXLEN   1024
#define CDC_FIELD_MAXLEN       8192
#define CDC_REQUESTLINE_MAXLEN 8192

#define CDC_UNDEFINED                    0
#define CDC_ALLOC                        1
#define CDC_STATE_WAIT_FOR_AUTH          2
#define CDC_STATE_AUTH_OK                3
#define CDC_STATE_AUTH_FAILED            4
#define CDC_STATE_AUTH_ERR               5
#define CDC_STATE_AUTH_NO_SESSION        6
#define CDC_STATE_REGISTRATION           7
#define CDC_STATE_HANDLE_REQUEST         8
#define CDC_STATE_CLOSE                  9

#define CDC_UUID_LEN 32
#define CDC_TYPE_LEN 16
/**
 * CDC session specific data
 */
typedef struct cdc_session
{
    char user[CDC_USER_MAXLEN + 1];            /*< username for authentication */
    char uuid[CDC_UUID_LEN + 1];               /*< client uuid in registration */
    unsigned int flags[2];                     /*< Received flags              */
    uint8_t  auth_data[SHA_DIGEST_LENGTH];     /*< Password Hash               */
    int state;                                 /*< CDC protocol state          */
} CDC_session;

/**
 * CDC protocol
 */
typedef struct  cdc_protocol
{
#ifdef SS_DEBUG
    skygw_chk_t protocol_chk_top;
#endif
    int state;                      /*< CDC protocol state          */
    char user[CDC_USER_MAXLEN + 1]; /*< username for authentication */
    SPINLOCK lock;                  /*< Protocol structure lock     */
    char type[CDC_TYPE_LEN + 1];    /*< Request Type            */
#ifdef SS_DEBUG
    skygw_chk_t protocol_chk_tail;
#endif
} CDC_protocol;

/* routines */
extern int gw_hex2bin(uint8_t *out, const char *in, unsigned int len);

MXS_END_DECLS

#endif
