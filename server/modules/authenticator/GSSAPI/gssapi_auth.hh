#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
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
#include <maxscale/protocol/mariadb/authenticator.hh>

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

class GSSAPIAuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    static GSSAPIAuthenticatorModule* create(char** options);
    ~GSSAPIAuthenticatorModule() override = default;

    mariadb::SClientAuth  create_client_authenticator() override;
    mariadb::SBackendAuth create_backend_authenticator() override;

    int         load_users(SERVICE* service) override;
    json_t*     diagnostics() override;
    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    char* principal_name {nullptr};     /**< Service principal name given to the client */

private:
    sqlite3* handle {nullptr};          /**< SQLite3 database handle */
};

class GSSAPIClientAuthenticator : public mariadb::ClientAuthenticatorT<GSSAPIAuthenticatorModule>
{
public:
    GSSAPIClientAuthenticator(GSSAPIAuthenticatorModule* module);
    ~GSSAPIClientAuthenticator() override;
    bool    extract(GWBUF* buffer, MYSQL_session* session) override;
    AuthRes authenticate(DCB* client) override;

    sqlite3* handle {nullptr};              /**< SQLite3 database handle */
    uint8_t  sequence {0};                  /**< The next packet seqence number */

private:
    void copy_client_information(GWBUF* buffer);
    bool store_client_token(MYSQL_session* session, GWBUF* buffer);

    gssapi_auth_state state {GSSAPI_AUTH_INIT};     /**< Authentication state*/
    uint8_t*          principal_name {nullptr};     /**< Principal name */
};

class GSSAPIBackendAuthenticator : public mariadb::BackendAuthenticator
{
public:
    ~GSSAPIBackendAuthenticator() override;
    bool    extract(DCB* backend, GWBUF* buffer) override;
    bool    ssl_capable(DCB* backend) override;
    AuthRes authenticate(DCB* backend) override;

private:
    bool extract_principal_name(DCB* dcb, GWBUF* buffer);
    bool send_new_auth_token(DCB* dcb);

    gssapi_auth_state state {GSSAPI_AUTH_INIT};     /**< Authentication state*/
    uint8_t*          principal_name {nullptr};     /**< Principal name */
    uint8_t           sequence {0};                 /**< The next packet sequence number */
    sqlite3*          handle {nullptr};             /**< SQLite3 database handle */
};
