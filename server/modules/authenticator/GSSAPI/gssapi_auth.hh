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
#include <stdint.h>
#include <stddef.h>
#include <gssapi.h>
#include <maxscale/sqlite3.h>

MXS_BEGIN_DECLS

/** Client auth plugin name */
static const char auth_plugin_name[] = "auth_gssapi_client";

/** This is mainly for testing purposes */
static const char default_princ_name[] = "mariadb/localhost.localdomain";

/** GSSAPI authentication states */
enum gssapi_auth_state
{
    GSSAPI_AUTH_INIT = 0,
    GSSAPI_AUTH_DATA_SENT,
    GSSAPI_AUTH_OK,
    GSSAPI_AUTH_FAILED
};

/** Common structure for both backend and client authenticators */
typedef struct gssapi_auth
{
    enum gssapi_auth_state state;               /**< Authentication state*/
    uint8_t*               principal_name;      /**< Principal name */
    size_t                 principal_name_len;  /**< Length of the principal name */
    uint8_t                sequence;            /**< The next packet seqence number */
    sqlite3*               handle;              /**< SQLite3 database handle */
} gssapi_auth_t;

/** Report GSSAPI errors */
void report_error(OM_uint32 major, OM_uint32 minor);

MXS_END_DECLS
