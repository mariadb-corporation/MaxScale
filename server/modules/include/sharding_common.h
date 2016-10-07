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

#ifndef _SHARDING_COMMON_HG
#define _SHARDING_COMMON_HG

#include <my_config.h>
#include <poll.h>
#include <buffer.h>
#include <modutil.h>
#include <mysql_client_server_protocol.h>
#include <hashtable.h>
#include <log_manager.h>
#include <query_classifier.h>

bool extract_database(GWBUF* buf, char* str);
void create_error_reply(char* fail_str, DCB* dcb);
bool change_current_db(char* dest, HASHTABLE* dbhash, GWBUF* buf);

#endif
