/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "postgresprotocol.hh"
#include "pgprotocoldata.hh"
#include <maxscale/protocol2.hh>
#include <deque>
#include <tuple>
#include <memory>

class PgProtocolData;
class PgBackendAuthenticator;

class PgBackendConnection final : public mxs::BackendConnection
{
public:
    PgBackendConnection(MXS_SESSION* session, SERVER* server, mxs::Component* component);

    void ready_for_reading(DCB* dcb) override;
    void error(DCB* dcb, const char* errmsg) override;

    bool routeQuery(GWBUF&& buffer) override;

    void finish_connection() override;

    uint64_t can_reuse(MXS_SESSION* session) const override;
    bool     reuse(MXS_SESSION* session, mxs::Component* upstream, uint64_t reuse_type) override;
    bool     established() override;
    bool     is_idle() const override;
    void     set_to_pooled() override;
    void     ping() override;
    bool     can_close() const override;

    void              set_dcb(DCB* dcb) override;
    const BackendDCB* dcb() const override;
    BackendDCB*       dcb() override;
    mxs::Component*   upstream() const override;

    json_t* diagnostics() const override;
    size_t  sizeof_buffers() const override;

    uint32_t pid()
    {
        return m_process_id;
    }

    uint32_t secret()
    {
        return m_secret_key;
    }

private:
    enum class State
    {
        INIT,
        SSL_REQUEST,
        SSL_HANDSHAKE,
        AUTH,
        STARTUP,
        HISTORY,
        ROUTING,
        REUSE,
        PING,
        FAILED,
    };

    // Struct that contains the information needed to correctly track the execution of queries
    struct TrackedQuery
    {
        TrackedQuery(const GWBUF& buffer);

        uint8_t  command;   // The command byte
        size_t   size;      // The size of the whole network payload, includes the command byte
        uint32_t id;        // The unique ID this command, set by the client protocol
    };

    bool check_size(const GWBUF& buffer, size_t bytes);
    void handle_error(const std::string& error, mxs::ErrorType type = mxs::ErrorType::TRANSIENT);
    void history_mismatch();
    void send_ssl_request();
    void send_startup_message();
    void send_history();
    void send_backlog();

    bool handle_ssl_request();
    bool handle_ssl_handshake();
    bool handle_startup();
    bool handle_auth();
    bool handle_history();
    bool handle_routing();
    bool handle_reuse();
    bool handle_ping();

    GWBUF read_complete_packets();
    GWBUF process_packets(GWBUF& buffer);
    void  track_query(const GWBUF& buffer);
    void  start_tracking(const TrackedQuery& query);
    bool  track_next_result();

    MXS_SESSION*    m_session;
    mxs::Component* m_upstream;
    BackendDCB*     m_dcb;
    mxs::Reply      m_reply;
    State           m_state {State::INIT};

    PgProtocolData*                         m_protocol_data {nullptr};
    std::unique_ptr<PgBackendAuthenticator> m_authenticator;

    uint32_t m_process_id {0};      // The process ID on the backend server
    uint32_t m_secret_key {0};      // Secret key for canceling requests

    // Backlog of packets that need to be written again. These are only buffered for the duratio of the
    // connection creation and authentication after which they are re-sent to the write() function.
    std::vector<GWBUF> m_backlog;

    // A queue of commands that are being executed. The queue will be empty if only one result is expected.
    std::deque<TrackedQuery> m_track_queue;

    // The session command history subscriber. This is what tracks the responses to session commands and makes
    // sure the response from this backend matches the one that was expected.
    std::unique_ptr<mxs::History::Subscriber> m_subscriber;

    // The identity of a client connection. The first value is the username and the second one is the default
    // database. The client identity is used to detect whether a user can safely reuse this connection. Since
    // this is only needed while the connection is in the pool, it can be allocated when the connection is
    // placed into the pool and freed when it's taken out of it.
    using ClientIdentity = std::tuple<std::string, std::string>;
    std::unique_ptr<ClientIdentity> m_identity;
};
