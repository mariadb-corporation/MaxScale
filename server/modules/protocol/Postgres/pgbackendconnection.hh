/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "postgresprotocol.hh"
#include "pgprotocoldata.hh"
#include <maxscale/protocol2.hh>

class PgBackendConnection : public mxs::BackendConnection
{
public:
    PgBackendConnection(MXS_SESSION* session, SERVER* server, mxs::Component* component);

    void ready_for_reading(DCB* dcb) override final;
    void write_ready(DCB* dcb) override final;
    void error(DCB* dcb) override final;
    void hangup(DCB* dcb) override final;

    bool write(GWBUF&& buffer) override final;

    void finish_connection() override final;

    uint64_t can_reuse(MXS_SESSION* session) const override final;
    bool     reuse(MXS_SESSION* session, mxs::Component* upstream, uint64_t reuse_type) override final;
    bool     established() override final;
    void     set_to_pooled() override final;
    void     ping() override final;
    bool     can_close() const override final;

    void              set_dcb(DCB* dcb) override final;
    const BackendDCB* dcb() const override final;
    BackendDCB*       dcb() override final;
    mxs::Component*   upstream() const override final;

    json_t* diagnostics() const override final;
    size_t  sizeof_buffers() const override final;

private:
    enum class State
    {
        INIT,
        SSL_REQUEST,
        SSL_HANDSHAKE,
        STARTUP,
        AUTH,
        BACKLOG,
        ROUTING,
        FAILED,
    };

    void handle_error(const std::string& error, mxs::ErrorType type = mxs::ErrorType::TRANSIENT);
    void send_ssl_request();
    void send_startup_message();

    bool handle_ssl_request();
    bool handle_ssl_handshake();
    bool handle_startup();
    bool handle_auth();
    bool handle_backlog();
    bool handle_routing();

    MXS_SESSION*    m_session;
    mxs::Component* m_upstream;
    BackendDCB*     m_dcb;
    mxs::Reply      m_reply;
    State           m_state {State::INIT};
};
