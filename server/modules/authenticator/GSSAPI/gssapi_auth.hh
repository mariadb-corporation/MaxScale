#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#define MXS_MODULE_NAME "GSSAPIAuth"

#include <maxscale/ccdefs.hh>
#include <stdint.h>
#include <stddef.h>
#include <gssapi.h>
#include <maxscale/sqlite3.h>
#include <maxscale/authenticator2.hh>

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

/** Report GSSAPI errors */
void report_error(OM_uint32 major, OM_uint32 minor);

class GSSAPIBackendAuthenticatorSession : public mxs::AuthenticatorBackendSession
{
public:
    static GSSAPIBackendAuthenticatorSession* newSession();

    ~GSSAPIBackendAuthenticatorSession() override;
    bool extract(DCB* backend, GWBUF* buffer) override;
    bool ssl_capable(DCB* backend) override;
    int authenticate(DCB* backend) override;

    gssapi_auth_state state;               /**< Authentication state*/
    uint8_t*               principal_name;      /**< Principal name */
    size_t                 principal_name_len;  /**< Length of the principal name */
    uint8_t                sequence;            /**< The next packet seqence number */
    sqlite3*               handle;              /**< SQLite3 database handle */

private:
    bool extract_principal_name(DCB* dcb, GWBUF* buffer);
    bool send_new_auth_token(DCB* dcb);
};

class GSSAPIAuthenticatorSession : public mxs::AuthenticatorSession
{
public:
    ~GSSAPIAuthenticatorSession() override;
    bool extract(DCB* client, GWBUF* buffer) override;
    bool ssl_capable(DCB* client) override;
    int authenticate(DCB* client) override;
    void free_data(DCB* client) override;

    GSSAPIBackendAuthenticatorSession* newBackendSession() override;

    gssapi_auth_state state;               /**< Authentication state*/
    uint8_t*               principal_name;      /**< Principal name */
    size_t                 principal_name_len;  /**< Length of the principal name */
    uint8_t                sequence;            /**< The next packet seqence number */
    sqlite3*               handle;              /**< SQLite3 database handle */

private:
    void copy_client_information(DCB* dcb, GWBUF* buffer);
    bool store_client_token(DCB* dcb, GWBUF* buffer);
};
