/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "pam_auth_common.hh"
#include <cstdint>
#include <string>
#include <vector>
#include <maxbase/pam_utils.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

/** Client authenticator PAM-specific session data */
class PamClientAuthenticator : public mariadb::ClientAuthenticator
{
public:
    using AuthMode = mxb::pam::AuthMode;
    PamClientAuthenticator(AuthSettings settings, const PasswordMap& backend_pwds);

    ExchRes exchange(GWBUF* read_buffer, MYSQL_session* session, AuthenticationData& auth_data) override;
    AuthRes authenticate(MYSQL_session* session, AuthenticationData& auth_data) override;

private:
    maxscale::Buffer create_auth_change_packet() const;

    enum class State
    {
        INIT,
        ASKED_FOR_PW,
        ASKED_FOR_2FA,
        PW_RECEIVED,
        DONE
    };

    State              m_state {State::INIT};       /**< Authentication state */
    const AuthSettings m_settings;
    const PasswordMap& m_backend_pwds;

    maxscale::Buffer create_2fa_prompt_packet() const;
};
