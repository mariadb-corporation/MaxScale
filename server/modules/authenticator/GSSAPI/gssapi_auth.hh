#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
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

/** GSSAPI authentication states */
enum gssapi_auth_state
{
    GSSAPI_AUTH_INIT = 0,
    GSSAPI_AUTH_DATA_SENT,
    GSSAPI_AUTH_TOKEN_READY,
    GSSAPI_AUTH_OK,
    GSSAPI_AUTH_FAILED
};

/** Report GSSAPI errors */
void report_error(OM_uint32 major, OM_uint32 minor);

class GSSAPIAuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    static GSSAPIAuthenticatorModule* create(mxs::ConfigParameters* options);
    ~GSSAPIAuthenticatorModule() override = default;

    mariadb::SClientAuth  create_client_authenticator() override;
    mariadb::SBackendAuth create_backend_authenticator(mariadb::BackendAuthData& auth_data) override;

    json_t*     diagnostics() override;
    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    const std::unordered_set<std::string>& supported_plugins() const override;

    std::string principal_name;     /**< Service principal name given to the client */
};

class GSSAPIClientAuthenticator : public mariadb::ClientAuthenticatorT<GSSAPIAuthenticatorModule>
{
public:
    GSSAPIClientAuthenticator(GSSAPIAuthenticatorModule* module);
    ~GSSAPIClientAuthenticator() override;

    ExchRes exchange(GWBUF* buffer, MYSQL_session* session, mxs::Buffer* output) override;
    AuthRes authenticate(const mariadb::UserEntry* entry, MYSQL_session* session) override;

    uint8_t m_sequence {0};                 /**< The next packet sequence number */

private:
    void copy_client_information(GWBUF* buffer);
    bool store_client_token(MYSQL_session* session, GWBUF* buffer);
    bool validate_gssapi_token(uint8_t* token, size_t len, char** output);
    bool validate_user(MYSQL_session* session, const char* princ,
                       const mariadb::UserEntry* entry);
    GWBUF* create_auth_change_packet();

    gssapi_auth_state state {GSSAPI_AUTH_INIT};     /**< Authentication state*/
    uint8_t*          principal_name {nullptr};     /**< Principal name */
};

class GSSAPIBackendAuthenticator : public mariadb::BackendAuthenticator
{
public:
    GSSAPIBackendAuthenticator(const mariadb::BackendAuthData& shared_data);
    ~GSSAPIBackendAuthenticator() override;
    bool    extract(DCB* backend, GWBUF* buffer) override;
    bool    ssl_capable(DCB* backend) override;
    AuthRes authenticate(DCB* backend) override;

private:
    bool extract_principal_name(GWBUF* buffer);
    bool send_new_auth_token(DCB* dcb);

    const mariadb::BackendAuthData& m_shared_data; /**< Data shared with backend connection */

    gssapi_auth_state state {GSSAPI_AUTH_INIT};     /**< Authentication state*/
    uint8_t*          principal_name {nullptr};     /**< Principal name */
    uint8_t           sequence {0};                 /**< The next packet sequence number */
};
