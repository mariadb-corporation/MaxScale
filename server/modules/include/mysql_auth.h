#ifndef _MYSQL_AUTH_H
#define _MYSQL_AUTH_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 02/02/2016   Martin Brampton         Initial implementation
 *
 * @endverbatim
 */

#include <dcb.h>
#include <buffer.h>
#include <stdint.h>
#include <mysql_client_server_protocol.h>

int gw_check_mysql_scramble_data(DCB *dcb,
                                 uint8_t *token,
                                 unsigned int token_len,
                                 uint8_t *scramble,
                                 unsigned int scramble_len,
                                 char *username,
                                 uint8_t *stage1_hash);
int check_db_name_after_auth(DCB *dcb, char *database, int auth_ret);
int gw_find_mysql_user_password_sha1(
    char *username,
    uint8_t *gateway_password,
    DCB *dcb);

#endif /** _MYSQL_AUTH_H */
