/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
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
    PamClientAuthenticator(bool cleartext_plugin, AuthMode mode);

    ExchRes exchange(GWBUF* read_buffer, MYSQL_session* session, mxs::Buffer* output_packet) override;
    AuthRes authenticate(const mariadb::UserEntry* entry, MYSQL_session* session) override;

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

    State   m_state {State::INIT};      /**< Authentication state */
    uint8_t m_sequence {0};             /**< The next packet sequence number */

    const bool     m_cleartext_plugin {false};  /**< Is "pam_use_cleartext_plugin" enabled? */
    const AuthMode m_mode {AuthMode::PW};

    maxscale::Buffer create_2fa_prompt_packet() const;
};
