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
#include <maxbase/pam_utils.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

class PamBackendAuthenticator : public mariadb::BackendAuthenticator
{
public:
    using AuthMode = mxb::pam::AuthMode;
    PamBackendAuthenticator(const PamBackendAuthenticator& orig) = delete;
    PamBackendAuthenticator& operator=(const PamBackendAuthenticator&) = delete;
    PamBackendAuthenticator(mariadb::BackendAuthData& shared_data, AuthMode mode);

    AuthRes exchange(const mxs::Buffer& input, mxs::Buffer* output) override;

private:
    enum class PromptType
    {
        FAIL,
        PASSWORD,
        TWO_FA
    };

    PromptType  parse_password_prompt(mariadb::ByteVec& data);
    mxs::Buffer generate_pw_packet(PromptType pw_type) const;

    enum class State
    {
        EXPECT_AUTHSWITCH,
        EXCHANGING,
        EXCHANGE_DONE,
        ERROR,
    };

    const mariadb::BackendAuthData& m_shared_data;  /**< Data shared with backend connection */
    const std::string               m_clienthost;   /**< Client 'name'@'host', for logging. */
    const AuthMode                  m_mode {AuthMode::PW};

    State   m_state {State::EXPECT_AUTHSWITCH}; /**< Authentication state */
    uint8_t m_sequence {0};                     /**< The next packet sequence number */
};
