#ifndef _MYSQL_AUTH_H
#define _MYSQL_AUTH_H
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

int mysql_auth_set_protocol_data(DCB *dcb, GWBUF *buf);
bool mysql_auth_is_client_ssl_capable (DCB *dcb);
int mysql_auth_authenticate(DCB *dcb, GWBUF **buf);
int gw_check_mysql_scramble_data(DCB *dcb,
                                 uint8_t *token,
                                 unsigned int token_len,
                                 uint8_t *scramble,
                                 unsigned int scramble_len,
                                 char *username,
                                 uint8_t *stage1_hash);
int check_db_name_after_auth(DCB *dcb, char *database, int auth_ret);

#endif /** _MYSQL_AUTH_H */
