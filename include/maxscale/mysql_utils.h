#ifndef _MYSQL_UTILS_H
#define _MYSQL_UTILS_H
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

#include <stdlib.h>
#include <stdint.h>
#include <mysql.h>
#include <maxscale/server.h>

/** Length-encoded integers */
size_t leint_bytes(uint8_t* ptr);
uint64_t leint_value(uint8_t* c);
uint64_t leint_consume(uint8_t ** c);

/** Length-encoded strings */
char* lestr_consume_dup(uint8_t** c);
char* lestr_consume(uint8_t** c, size_t *size);


MYSQL *mxs_mysql_real_connect(MYSQL *mysql, SERVER *server, const char *user, const char *passwd);

#endif
