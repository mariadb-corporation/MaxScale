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
#include <maxscale/session.hh>
#include "nosqlbase.hh"
#include "nosqlsasl.hh"

class ClientConnection;

namespace nosql
{
class UserManager;

class Context
{
public:
    Context(const Context&) = delete;
    Context& operator = (const Context&) = delete;

    Context(UserManager* pUm,
            MXS_SESSION* pSession,
            ClientConnection* pClient_connection,
            mxs::Component* pDownstream);

    UserManager& um() const
    {
        return m_um;
    }

    ClientConnection& client_connection()
    {
        return m_client_connection;
    }

    MXS_SESSION& session()
    {
        return m_session;
    }

    mxs::Component& downstream()
    {
        return m_downstream;
    }

    int64_t connection_id() const
    {
        return m_connection_id;
    }

    int32_t current_request_id() const
    {
        return m_request_id;
    }

    int32_t next_request_id()
    {
        return ++m_request_id;
    }

    void set_last_error(std::unique_ptr<LastError>&& sLast_error)
    {
        m_sLast_error = std::move(sLast_error);
    }

    void get_last_error(DocumentBuilder& doc);
    void reset_error(int32_t n = 0);

    mxs::RoutingWorker& worker() const
    {
        mxb_assert(m_session.worker());
        return *m_session.worker();
    }

    void set_metadata_sent(bool metadata_sent)
    {
        m_metadata_sent = metadata_sent;
    }

    bool metadata_sent() const
    {
        return m_metadata_sent;
    }

    std::unique_ptr<Sasl> get_sasl()
    {
        return std::move(m_sSasl);
    }

    void put_sasl(std::unique_ptr<Sasl> sSasl)
    {
        m_sSasl = std::move(sSasl);
    }

    void set_roles(std::unordered_map<std::string, uint32_t>&& roles)
    {
        m_roles = roles;
    }

    uint32_t role_mask_of(const std::string& name) const
    {
        auto it = m_roles.find(name);

        return it == m_roles.end() ? 0 : it->second;
    }

    bool authenticated() const
    {
        return !m_authentication_db.empty();
    }

    const std::string& authentication_db() const
    {
        return m_authentication_db;
    }

    void set_authenticated(const std::string& authentication_db)
    {
        m_authentication_db = authentication_db;
    }

    void set_unauthenticated()
    {
        m_authentication_db.clear();
    }

private:
    using Roles = std::unordered_map<std::string, uint32_t>;

    UserManager&               m_um;
    MXS_SESSION&               m_session;
    ClientConnection&          m_client_connection;
    mxs::Component&            m_downstream;
    int32_t                    m_request_id { 1 };
    int64_t                    m_connection_id;
    std::unique_ptr<LastError> m_sLast_error;
    bool                       m_metadata_sent { false };
    std::unique_ptr<Sasl>      m_sSasl;
    Roles                      m_roles;
    std::string                m_authentication_db;

    static std::atomic<int64_t> s_connection_id;
};

}
