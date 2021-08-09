#pragma once
/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <gssapi.h>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

class GSSAPIAuthenticatorModule;

/** GSSAPI authentication states */
enum gssapi_auth_state
{
    GSSAPI_AUTH_INIT = 0,
    GSSAPI_AUTH_DATA_SENT,
    GSSAPI_AUTH_TOKEN_READY,
    GSSAPI_AUTH_OK,
    GSSAPI_AUTH_FAILED
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
