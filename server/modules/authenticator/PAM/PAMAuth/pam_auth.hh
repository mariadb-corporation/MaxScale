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

/*
 * Common definitions and includes for PAM client authenticator
 */
#define MXS_MODULE_NAME "PAMAuth"

#include <maxscale/ccdefs.hh>

#include <string>
#include <maxscale/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.h>
#include <maxscale/protocol/mysql.h>

using std::string;

extern const string FIELD_USER;
extern const string FIELD_HOST;
extern const string FIELD_DB;
extern const string FIELD_ANYDB;
extern const string FIELD_AUTHSTR;
extern const int NUM_FIELDS;