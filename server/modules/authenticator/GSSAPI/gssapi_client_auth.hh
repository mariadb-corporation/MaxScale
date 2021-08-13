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

class GSSAPIClientAuthenticator : public mariadb::ClientAuthenticator
{
public:
    GSSAPIClientAuthenticator(const std::string& service_principal);

    ExchRes exchange(GWBUF* buffer, MYSQL_session* session, mxs::Buffer* output) override;
    AuthRes authenticate(const mariadb::UserEntry* entry, MYSQL_session* session) override;

private:
    void copy_client_information(GWBUF* buffer);
    bool store_client_token(MYSQL_session* session, GWBUF* buffer);
    bool validate_gssapi_token(uint8_t* token, size_t len, char** output);
    bool validate_user(MYSQL_session* session, const char* princ,
                       const mariadb::UserEntry* entry);
    GWBUF* create_auth_change_packet();

    enum class State
    {
        INIT,
        DATA_SENT,
        TOKEN_READY,
    };

    State              m_state {State::INIT};   /**< Authentication state*/
    uint8_t            m_sequence {0};          /**< The next packet sequence number */
    const std::string& m_service_principal;     /**< Service principal */
};
