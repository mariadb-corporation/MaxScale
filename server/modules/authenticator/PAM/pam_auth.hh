/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/*
 * Common definitions and includes for PAM client authenticator
 */
#define MXS_MODULE_NAME "PAMAuth"

#include <maxscale/ccdefs.hh>

#include <maxscale/authenticator2.hh>
#include <string>
#include <maxbase/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

using std::string;

extern const string FIELD_USER;
extern const string FIELD_HOST;
extern const string FIELD_DB;
extern const string FIELD_ANYDB;
extern const string FIELD_AUTHSTR;
extern const string FIELD_DEF_ROLE;
extern const string FIELD_HAS_PROXY;
extern const string FIELD_IS_ROLE;
extern const string FIELD_ROLE;
extern const int NUM_FIELDS;

extern const char* SQLITE_OPEN_FAIL;
extern const char* SQLITE_OPEN_OOM;

extern const string TABLE_USER;
extern const string TABLE_DB;
extern const string TABLE_ROLES_MAPPING;
