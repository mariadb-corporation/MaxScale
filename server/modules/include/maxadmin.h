#ifndef _MAXADMIN_H
#define _MAXADMIN_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MAXADMIN_DEFAULT_SOCKET                "/tmp/maxadmin.sock"

#define MAXADMIN_CONFIG_DEFAULT_SOCKET_TAG_LEN 7
#define MAXADMIN_CONFIG_DEFAULT_SOCKET_TAG     "default"

#define MAXADMIN_AUTH_REPLY_LEN                6
#define MAXADMIN_AUTH_FAILED_REPLY             "FAILED"
#define MAXADMIN_AUTH_SUCCESS_REPLY            "OK----"

#define MAXADMIN_AUTH_USER_PROMPT              "USER"
#define MAXADMIN_AUTH_USER_PROMPT_LEN          4

#define MAXADMIN_AUTH_PASSWORD_PROMPT          "PASSWORD"
#define MAXADMIN_AUTH_PASSWORD_PROMPT_LEN      8

#endif
