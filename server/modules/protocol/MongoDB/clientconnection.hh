/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "mongodbclient.hh"
#include <maxscale/protocol2.hh>

namespace mxsmongo
{

class ClientConnection : public mxs::ClientConnection
{
public:
    enum class State
    {
        CONNECTED,
        HANDSHAKING,
        READY,
    };

    ClientConnection(MXS_SESSION* pSession, mxs::Component* pComponent);
    ~ClientConnection();

    bool init_connection() override;

    void finish_connection() override;

    ClientDCB* dcb() override;
    const ClientDCB* dcb() const override;

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
    void handle_one_packet(GWBUF* pPacket);
    void handle_packet_query(GWBUF* pPacket);

private:
    State           m_state;
    MXS_SESSION&    m_session;
    mxs::Component& m_component;
    DCB*            m_pDcb = nullptr;
};

}
