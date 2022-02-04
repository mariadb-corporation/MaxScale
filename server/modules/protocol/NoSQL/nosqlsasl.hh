/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include "nosqlscram.hh"
#include "nosqlusermanager.hh"

namespace nosql
{

class Sasl
{
public:
    const UserManager::UserInfo& user_info() const
    {
        return m_user_info;
    }

    int32_t conversation_id() const
    {
        return m_conversation_id;
    }

    int32_t bump_conversation_id()
    {
        return ++m_conversation_id;
    }

    const std::string& client_nonce_b64() const
    {
        return m_client_nonce_b64;
    }

    const std::string& gs2_header() const
    {
        return m_gs2_header;
    }

    const std::string& server_nonce_b64() const
    {
        return m_server_nonce_b64;
    }

    std::string nonce_b64() const
    {
        return m_client_nonce_b64 + m_server_nonce_b64;
    }

    const std::string& initial_message() const
    {
        return m_initial_message;
    }

    const std::string& server_first_message() const
    {
        return m_server_first_message;
    }

    scram::Mechanism mechanism() const
    {
        return m_mechanism;
    }

    void set_client_nonce_b64(const std::string s)
    {
        m_client_nonce_b64 = std::move(s);
    }

    void set_client_nonce_b64(const string_view& s)
    {
        set_client_nonce_b64(to_string(s));
    }

    void set_gs2_header(const std::string s)
    {
        m_gs2_header = std::move(s);
    }

    void set_gs2_header(const string_view& s)
    {
        set_gs2_header(to_string(s));
    }

    void set_server_nonce_b64(std::string s)
    {
        m_server_nonce_b64 = std::move(s);
    }

    void set_server_nonce_b64(const std::vector<uint8_t>& v)
    {
        set_server_nonce_b64(std::string(reinterpret_cast<const char*>(v.data()), v.size()));
    }

    void set_initial_message(std::string s)
    {
        m_initial_message = std::move(s);
    }

    void set_initial_message(const string_view& s)
    {
        set_initial_message(to_string(s));
    }

    void set_server_first_message(std::string s)
    {
        m_server_first_message = std::move(s);
    }

    void set_user_info(UserManager::UserInfo&& user_info)
    {
        m_user_info = std::move(user_info);
    }

    void set_mechanism(scram::Mechanism m)
    {
        m_mechanism = m;
    }

private:
    UserManager::UserInfo m_user_info;
    std::string           m_client_nonce_b64;
    std::string           m_gs2_header;
    std::string           m_server_nonce_b64;
    int32_t               m_conversation_id { 0 };
    std::string           m_initial_message;
    std::string           m_server_first_message;
    scram::Mechanism      m_mechanism = scram::Mechanism::SHA_1;
};

}
