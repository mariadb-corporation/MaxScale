/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/target.hh>
#include "session.hh"

namespace maxscale
{

namespace mock
{
class Endpoint final : public mxs::Endpoint
{
public:
    Endpoint(FilterModule::Session* pSession)
        : m_session(*pSession)
    {
    }

    bool routeQuery(GWBUF&& buffer) override
    {
        return m_session.routeQuery(std::move(buffer));
    }

    bool clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override
    {
        return 0;
    }

    bool handleError(mxs::ErrorType type, const std::string& error, mxs::Endpoint* down,
                     const mxs::Reply& reply) override
    {
        return true;
    }

    bool connect() override
    {
        return true;
    }

    void close() override
    {
        m_open = false;
    }

    bool is_open() const override
    {
        return m_open;
    }

    mxs::Target* target() const override
    {
        return nullptr;
    }

    mxs::Component* parent() const override
    {
        return nullptr;
    }

private:
    FilterModule::Session& m_session;
    bool                   m_open = true;
};
}
}
