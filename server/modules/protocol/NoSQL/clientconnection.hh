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
#include <maxscale/protocol2.hh>
#include "config.hh"
#include "nosql.hh"
#include "nosqlusermanager.hh"

class MYSQL_session;

class ClientConnection : public mxs::ClientConnection
{
public:
    enum State
    {
        CONNECTED,
        READY
    };

    ClientConnection(const GlobalConfig& config,
                     nosql::UserManager* pUm,
                     MXS_SESSION* pSession,
                     mxs::Component* pComponent);
    ~ClientConnection();

    bool init_connection() override;

    void finish_connection() override;

    ClientDCB* dcb() override;
    const ClientDCB* dcb() const override;

    bool clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool in_routing_state() const override
    {
        return true;
    }

private:
    // DCBHandler
    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

private:
    // mxs::ProtocolConnection
    int32_t write(GWBUF* buffer) override;
    json_t* diagnostics() const override;
    void set_dcb(DCB* dcb) override;
    bool is_movable() const override;

private:
    bool is_ready() const
    {
        return m_state == READY;
    }

    void set_ready()
    {
        m_state = READY;
    }

    bool setup_session();

    GWBUF* handle_one_packet(GWBUF* pPacket);

private:
    State          m_state { CONNECTED };
    nosql::Config  m_config;
    MXS_SESSION&   m_session;
    MYSQL_session& m_session_data;
    DCB*           m_pDcb = nullptr;
    nosql::NoSQL   m_nosql;
};
