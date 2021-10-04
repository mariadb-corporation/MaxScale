/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "gssapi_backend_auth.hh"

#include <maxbase/alloc.h>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

/**
 * Generate packet with client password in cleartext.
 *
 * @return Packet with password
 */
mxs::Buffer GSSAPIBackendAuthenticator::generate_auth_token_packet() const
{
    const auto& auth_token = m_shared_data.client_data->auth_data.backend_token;
    auto auth_token_len = auth_token.size();
    size_t buflen = MYSQL_HEADER_LEN + auth_token_len;
    mxs::Buffer rval(buflen);
    auto* ptr = rval.data();
    ptr = mariadb::write_header(ptr, auth_token_len, m_sequence);
    if (auth_token_len > 0)
    {
        memcpy(ptr, auth_token.data(), auth_token_len);
    }
    return rval;
}

GSSAPIBackendAuthenticator::GSSAPIBackendAuthenticator(const mariadb::BackendAuthData& shared_data)
    : m_shared_data(shared_data)
{
}

mariadb::BackendAuthenticator::AuthRes
GSSAPIBackendAuthenticator::exchange(const mxs::Buffer& input, mxs::Buffer* output)
{
    const char plugin_name[] = "auth_gssapi_client";
    const char* srv_name = m_shared_data.servername;
    // Smallest buffer that is parsed, header + principal name (0-term).
    const int min_readable_buflen = MYSQL_HEADER_LEN + 2;
    const int buflen = input.length();
    if (buflen <= min_readable_buflen)
    {
        MXB_ERROR("Received packet of size %i from '%s' during authentication. Expected packet size is "
                  "at least %i.", buflen, srv_name, min_readable_buflen);
        return AuthRes::FAIL;
    }

    m_sequence = MYSQL_GET_PACKET_NO(GWBUF_DATA(input.get())) + 1;
    auto rval = AuthRes::FAIL;

    switch (m_state)
    {
    case State::EXPECT_AUTHSWITCH:
        {
            // Server should have sent the AuthSwitchRequest.
            auto parse_res = mariadb::parse_auth_switch_request(input);
            if (parse_res.success)
            {
                if (parse_res.plugin_name != plugin_name)
                {
                    MXB_ERROR(WRONG_PLUGIN_REQ, m_shared_data.servername, parse_res.plugin_name.c_str(),
                              m_shared_data.client_data->user_and_host().c_str(), plugin_name);
                }
                else if (!parse_res.plugin_data.empty())
                {
                    // Principal name sent by server is in parse result, but it's not required.
                    *output = generate_auth_token_packet();
                    m_state = State::TOKEN_SENT;
                    rval = AuthRes::SUCCESS;
                }
                else
                {
                    MXB_ERROR("Backend server did not send any auth plugin data.");
                }
            }
            else
            {
                // No AuthSwitchRequest, error.
                MXB_ERROR(MALFORMED_AUTH_SWITCH, m_shared_data.servername);
            }
        }
        break;

    case State::TOKEN_SENT:
        // Server is sending more packets than expected. Error.
        MXB_ERROR("Server '%s' sent more packets than expected.", m_shared_data.servername);
        break;


    case State::ERROR:
        // Should not get here.
        mxb_assert(!true);
        break;
    }

    if (rval != AuthRes::SUCCESS)
    {
        m_state = State::ERROR;
    }
    return rval;
}
