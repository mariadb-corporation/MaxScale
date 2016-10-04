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

#ifndef _GSSAPI_AUTH_H
#define _GSSAPI_AUTH_H


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

/** Common state tracking structure */
typedef struct gssapi_auth
{
    enum gssapi_auth_state state;
} gssapi_auth_t;

/** These functions can used for the `create` and `destroy` entry points */
void* gssapi_auth_alloc();
void gssapi_auth_free(void *data);

#endif
