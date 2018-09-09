#pragma once
#ifndef _TELNETD_H
#define _TELNETD_H
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

/**
 * @file telnetd.h The telnetd protocol module header file
 *
 * @verbatim
 * Revision History
 *
 * Date     Who             Description
 * 17/07/13 Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>

MXS_BEGIN_DECLS

/**
 * The telnetd specific protocol structure to put in the DCB.
 */
typedef struct telnetd
{
    int   state;                /**< The connection state */
    char* username;             /**< The login name of the user */
} TELNETD;

#define TELNETD_STATE_LOGIN  1  /**< Issued login prompt */
#define TELNETD_STATE_PASSWD 2  /**< Issued password prompt */
#define TELNETD_STATE_DATA   3  /**< User logged in */

#define TELNET_SE                240
#define TELNET_NOP               241
#define TELNET_DATA_MARK         242
#define TELNET_BRK               243
#define TELNET_IP                244
#define TELNET_AO                245
#define TELNET_AYT               246
#define TELNET_EC                247
#define TELNET_EL                248
#define TELNET_GA                249
#define TELNET_SB                250
#define TELNET_WILL              251
#define TELNET_WONT              252
#define TELNET_DO                253
#define TELNET_DONT              254
#define TELNET_IAC               255
#define TELNET_ECHO              1
#define TELNET_SUPPRESS_GO_AHEAD 3

MXS_END_DECLS

#endif
