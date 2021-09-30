#pragma once
/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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

    ExchRes exchange(GWBUF* buffer, MYSQL_session* session, AuthenticationData& auth_data) override;
    AuthRes authenticate(MYSQL_session* session, AuthenticationData& auth_data) override;

private:
    void store_client_token(MYSQL_session* session, GWBUF* buffer);
    bool validate_gssapi_token(AuthenticationData& auth_data);

    mxs::Buffer create_auth_change_packet();

    enum class State
    {
        INIT,
        DATA_SENT,
        TOKEN_READY,
    };

    State              m_state {State::INIT};   /**< Authentication state*/
    const std::string& m_service_principal;     /**< Service principal */
};
