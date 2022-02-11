#pragma once
/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>

class GSSAPIBackendAuthenticator : public mariadb::BackendAuthenticator
{
public:
    GSSAPIBackendAuthenticator(const mariadb::BackendAuthData& shared_data);
    AuthRes exchange(const mxs::Buffer& input, mxs::Buffer* output) override;

private:
    mxs::Buffer generate_auth_token_packet() const;

    enum class State
    {
        EXPECT_AUTHSWITCH,
        TOKEN_SENT,
        ERROR
    };

    State   m_state {State::EXPECT_AUTHSWITCH};     /**< Authentication state*/
    uint8_t m_sequence {0};                         /**< The next packet sequence number */

    const mariadb::BackendAuthData& m_shared_data;      /**< Data shared with backend connection */
};
