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

#include "pgbackendconnection.hh"
#include <maxscale/server.hh>

namespace
{
GWBUF create_ssl_request()
{
    std::array<uint8_t, 8> buf{};
    pg::set_uint32(buf.data(), 8);
    pg::set_uint32(buf.data() + 4, pg::SSLREQ_MAGIC);
    return GWBUF(buf.data(), buf.size());
}

GWBUF create_startup_message(const std::vector<uint8_t>& params)
{
    // The parameters should be null-terminated
    mxb_assert(params.back() == 0x0);

    GWBUF rval(8 + params.size());
    uint8_t* ptr = rval.data();

    ptr += pg::set_uint32(ptr, 8 + params.size());
    ptr += pg::set_uint32(ptr, pg::PROTOCOL_V3_MAGIC);
    memcpy(ptr, params.data(), params.size());

    return rval;
}

GWBUF create_terminate()
{
    std::array<uint8_t, 5> buf{};
    buf[0] = 'X';
    pg::set_uint32(buf.data() + 1, 4);
    return GWBUF(buf.data(), buf.size());
}
}

PgBackendConnection::TrackedQuery::TrackedQuery(const GWBUF& buffer)
    : command(buffer[0])
    , size(buffer.length())
    , id(buffer.id())
{
}

PgBackendConnection::PgBackendConnection(MXS_SESSION* session, SERVER* server, mxs::Component* component)
    : m_session(session)
    , m_upstream(component)
    , m_protocol_data(static_cast<PgProtocolData*>(session->protocol_data()))
    , m_authenticator(m_protocol_data->auth_data().auth_module->create_backend_authenticator())
{
    auto fn = std::bind(&PgBackendConnection::history_mismatch, this);
    m_subscriber = m_protocol_data->history().subscribe(std::move(fn));
}

void PgBackendConnection::ready_for_reading(DCB* dcb)
{
    bool keep_going = true;

    while (keep_going)
    {
        switch (m_state)
        {
        case State::SSL_REQUEST:
            keep_going = handle_ssl_request();
            break;

        case State::SSL_HANDSHAKE:
            keep_going = handle_ssl_handshake();
            break;

        case State::AUTH:
            keep_going = handle_auth();
            break;

        case State::STARTUP:
            keep_going = handle_startup();
            break;

        case State::HISTORY:
            keep_going = handle_history();
            break;

        case State::ROUTING:
            keep_going = handle_routing();
            break;

        case State::REUSE:
            keep_going = handle_reuse();
            break;

        case State::PING:
            keep_going = handle_ping();
            break;

        case State::FAILED:
            keep_going = false;
            break;

        case State::INIT:
            mxb_assert_message(!true, "We should not end up here");
            handle_error("Internal error");
            keep_going = false;
            break;
        }
    }
}

void PgBackendConnection::error(DCB* dcb, const char* errmsg)
{
    m_upstream->handleError(mxs::ErrorType::TRANSIENT, errmsg, nullptr, m_reply);
}

bool PgBackendConnection::routeQuery(GWBUF&& buffer)
{
    if (m_state != State::ROUTING)
    {
        MXB_INFO("Store packet in backlog: %s", pg::describe(buffer).c_str());
        m_backlog.emplace_back(std::move(buffer));
        return true;
    }

    if (pg::will_respond(buffer))
    {
        track_query(buffer);
    }

    if (m_dcb->server()->persistent_conns_enabled() && buffer[0] == pg::TERMINATE)
    {
        // Don't route the Terminate message, this keeps the connection alive
        return true;
    }

    return m_dcb->writeq_append(std::move(buffer));
}

void PgBackendConnection::finish_connection()
{
    // Reset the subscriber now. This must be done here and not in the destructor.
    // See mxs::History::subscribe() for more information.
    m_subscriber.reset();

    m_dcb->writeq_append(create_terminate());
}

uint64_t PgBackendConnection::can_reuse(MXS_SESSION* session) const
{
    mxb_assert (session->protocol()->name() == MXS_POSTGRESQL_PROTOCOL_NAME);
    uint64_t reuse_type = REUSE_NOT_POSSIBLE;
    const auto& data = *static_cast<const PgProtocolData*>(session->protocol_data());

    if (m_identity && *m_identity == std::tie(session->user(), data.default_db()))
    {
        reuse_type = OPTIMAL_REUSE;
    }

    return reuse_type;
}

bool PgBackendConnection::reuse(MXS_SESSION* session, mxs::Component* component, uint64_t reuse_type)
{
    m_identity.reset();
    m_session = session;
    m_protocol_data = static_cast<PgProtocolData*>(session->protocol_data());
    m_upstream = component;

    auto fn = std::bind(&PgBackendConnection::history_mismatch, this);
    m_subscriber = m_protocol_data->history().subscribe(std::move(fn));

    MXB_INFO("Reusing connection");
    m_state = State::REUSE;

    // The DISCARD ALL resets the session state
    // https://www.postgresql.org/docs/current/sql-discard.html
    return m_dcb->writeq_append(pg::create_query_packet("DISCARD ALL"));
}

bool PgBackendConnection::established()
{
    return m_state == State::ROUTING;
}

bool PgBackendConnection::is_idle() const
{
    return m_state == State::ROUTING && m_reply.is_complete() && m_track_queue.empty();
}

void PgBackendConnection::set_to_pooled()
{
    m_subscriber.reset();
    m_identity = std::make_unique<ClientIdentity>(m_session->user(), m_protocol_data->default_db());
    m_session = nullptr;
    m_upstream = nullptr;
}

void PgBackendConnection::ping()
{
    m_state = State::PING;

    // A query with only a comment creates a very short response, shorter than a SELECT 1 would create. This
    // is similar to what the DBD::Pg Perl library uses for pinging the connection.
    auto query = pg::create_query_packet("/* ping */");
    track_query(query);
    m_dcb->writeq_append(std::move(query));
}

bool PgBackendConnection::can_close() const
{
    return true;
}

void PgBackendConnection::set_dcb(DCB* dcb)
{
    m_dcb = static_cast<BackendDCB*>(dcb);

    if (m_state == State::INIT)
    {
        // In the Postgres protocol, the client starts by sending a message
        if (m_dcb->using_ssl())
        {
            // If the server is configured to use TLS, send a SSLRequest message to see if the server has been
            // configured with TLS.
            send_ssl_request();
        }
        else
        {
            // If TLS is not configured, skip it and send the StartupMessage immediately
            send_startup_message();
        }
    }
}

const BackendDCB* PgBackendConnection::dcb() const
{
    return m_dcb;
}

BackendDCB* PgBackendConnection::dcb()
{
    return m_dcb;
}

mxs::Component* PgBackendConnection::upstream() const
{
    return m_upstream;
}

json_t* PgBackendConnection::diagnostics() const
{
    return nullptr;
}

size_t PgBackendConnection::sizeof_buffers() const
{
    return 0;
}

void PgBackendConnection::handle_error(const std::string& error, mxs::ErrorType type)
{
    // Release the subscription before calling handleError. This prevents the callback from being called while
    // the DCB is in the zombie queue.
    m_subscriber.reset();

    m_upstream->handleError(type, error, nullptr, m_reply);
    m_state = State::FAILED;
}

bool PgBackendConnection::check_size(const GWBUF& buffer, size_t bytes)
{
    bool ok = buffer.length() >= bytes;

    if (!ok)
    {
        std::ostringstream ss;
        ss << "Malformed packet, expected at least " << bytes << " bytes but have only " << buffer.length();
        handle_error(ss.str(), mxs::ErrorType::PERMANENT);
        mxb_assert_message(!true, "Not enough bytes");
    }

    return ok;
}

void PgBackendConnection::send_ssl_request()
{
    if (m_dcb->writeq_append(create_ssl_request()))
    {
        m_state = State::SSL_REQUEST;
    }
    else
    {
        handle_error("Failed to write SSL request");
    }
}

void PgBackendConnection::send_startup_message()
{
    // The parameters are a list of null-terminated strings that end with an empty string
    if (m_dcb->writeq_append(create_startup_message(m_protocol_data->connect_params())))
    {
        m_state = State::AUTH;
    }
    else
    {
        handle_error("Failed to write startup message");
    }
}

bool PgBackendConnection::handle_ssl_request()
{
    if (auto [ok, buf] = m_dcb->read_strict(1, 1); ok)
    {
        mxb_assert_message(buf, "There should always be data available");
        uint8_t response = buf[0];

        if (response == pg::SSLREQ_NO)
        {
            // No SSL, send the normal startup message.
            send_startup_message();
        }
        else if (response == pg::SSLREQ_YES)
        {
            // SSL requested, start the TLS handshake.
            if (m_dcb->ssl_handshake() == -1)
            {
                handle_error("TLS handshake failed");
            }
            else
            {
                m_state = State::SSL_HANDSHAKE;
            }
        }
        else
        {
            handle_error("Unknown response to SSL request");
        }
    }
    else
    {
        handle_error("Network read failed");
    }

    return m_state != State::FAILED;
}

bool PgBackendConnection::handle_ssl_handshake()
{
    bool keep_going = false;

    switch (m_dcb->ssl_state())
    {
    case DCB::SSLState::ESTABLISHED:
        send_startup_message();
        keep_going = true;
        break;

    case DCB::SSLState::HANDSHAKE_REQUIRED:
        // Handshake is still going on, wait for more data.
        break;

    default:
        handle_error("SSL handshake failed");
        break;
    }

    return keep_going;
}

bool PgBackendConnection::handle_startup()
{
    if (auto [ok, buf] = pg::read_packet(m_dcb); ok)
    {
        if (!buf)
        {
            // Partial read, try again later
            return false;
        }

        uint8_t command = buf[0];

        switch (command)
        {
        case pg::AUTHENTICATION:
            if (check_size(buf, pg::HEADER_LEN + 4))
            {
                auto auth_method = pg::get_uint32(buf.data() + pg::HEADER_LEN);
                handle_error(mxb::cat("Unexpected authentication message: ", std::to_string(auth_method)));
            }
            break;

        case pg::BACKEND_KEY_DATA:
            if (check_size(buf, pg::HEADER_LEN + 8))
            {
                // Stash the process ID and the key, we'll need it to kill this connection
                m_process_id = pg::get_uint32(buf.data() + pg::HEADER_LEN);
                m_secret_key = pg::get_uint32(buf.data() + pg::HEADER_LEN + 4);
            }
            break;

        case pg::PARAMETER_STATUS:
            // Server parameters, ignore these for now
            break;

        case pg::NOTICE_RESPONSE:
            // Notification of some sorts, ignore it
            MXB_INFO("Server notification: %s", pg::format_response(buf).c_str());
            break;

        case pg::READY_FOR_QUERY:
            // Authentication is successful.
            // TODO: Track the transaction status from this packet
            if (m_subscriber->history().empty())
            {
                m_state = State::ROUTING;
                send_backlog();
            }
            else
            {
                m_state = State::HISTORY;
                send_history();
            }
            break;

        case pg::ERROR_RESPONSE:
            handle_error("Authentication failed: " + pg::format_response(buf), mxs::ErrorType::PERMANENT);
            break;
        }
    }
    else
    {
        handle_error("Network read failed");
    }

    return true;
}

bool PgBackendConnection::handle_auth()
{
    if (auto [ok, buf] = pg::read_packet(m_dcb); ok)
    {
        if (!buf)
        {
            // Partial read, try again later
            return false;
        }

        uint8_t command = buf[0];

        switch (command)
        {
        case pg::AUTHENTICATION:
            if (check_size(buf, pg::HEADER_LEN + 4))
            {
                auto auth_method = pg::get_uint32(buf.data() + pg::HEADER_LEN);

                if (auth_method == pg::AUTH_OK)
                {
                    m_state = State::STARTUP;
                }
                else
                {
                    // Not an AuthenticationOk-packet. Give it to the authenticator and send the result
                    // back to server.
                    auto reply = m_authenticator->exchange(std::move(buf), *m_protocol_data);
                    if (reply)
                    {
                        if (!reply->empty())
                        {
                            m_dcb->writeq_append(std::move(*reply));
                        }
                    }
                    else
                    {
                        handle_error(mxb::cat("Unsupported authentication mechanism: ",
                                              std::to_string(auth_method)));
                    }
                }
            }
            break;

        case pg::ERROR_RESPONSE:
            handle_error("Authentication failed: " + pg::format_response(buf), mxs::ErrorType::PERMANENT);
            break;

        default:
            handle_error("Unknown command: " + std::to_string(command), mxs::ErrorType::PERMANENT);
            break;
        }
    }
    else
    {
        handle_error("Network read failed");
    }

    return true;
}

void PgBackendConnection::track_query(const GWBUF& buffer)
{
    mxb_assert(pg::will_respond(buffer) || pg::is_prepare(buffer));
    TrackedQuery query{buffer};

    if (m_reply.is_complete())
    {
        // The connection is idle, start tracking the result state
        start_tracking(query);
    }
    else
    {
        // Something else is already going on, store the information so that we can start tracking it once
        // the current command completes.
        m_track_queue.push_back(query);
    }
}

void PgBackendConnection::start_tracking(const TrackedQuery& query)
{
    m_reply.clear();
    m_reply.set_reply_state(mxs::ReplyState::START);
    m_reply.set_command(query.command);
    m_reply.add_upload_bytes(query.size);

    if (query.id)
    {
        m_subscriber->set_current_id(query.id);
    }
}

bool PgBackendConnection::track_next_result()
{
    bool more = !m_track_queue.empty();

    if (more)
    {
        start_tracking(m_track_queue.front());
        m_track_queue.pop_front();
    }

    return more;
}

void PgBackendConnection::send_history()
{
    for (const auto& buffer : m_subscriber->history())
    {
        MXB_INFO("Execute %u on '%s': %s", buffer.id(), m_dcb->server()->name(),
                 pg::describe(buffer).c_str());

        track_query(buffer);
        m_dcb->writeq_append(buffer.shallow_clone());
    }
}

bool PgBackendConnection::handle_history()
{
    mxb_assert_message(!m_reply.is_complete(), "A reply should always be expected at this point");
    bool keep_going = false;

    if (GWBUF packets = read_complete_packets())
    {
        if (m_reply.is_complete())
        {
            if (m_subscriber->add_response(!m_reply.error()))
            {
                MXB_INFO("Reply to %u complete", m_subscriber->current_id());

                // Keep reading more data until all the results have been read or we run out of data.
                keep_going = true;

                if (!track_next_result())
                {
                    // The history execution is now complete.
                    m_state = State::ROUTING;
                    send_backlog();
                }
            }
            else
            {
                history_mismatch();
            }
        }
    }

    return keep_going;
}

void PgBackendConnection::history_mismatch()
{
    std::ostringstream ss;

    ss << "Response from server '" << m_dcb->server()->name() << "' "
       << "differs from the expected response to " << static_cast<char>(m_reply.command()) << ". "
       << "Closing connection due to inconsistent session state.";

    if (m_reply.error())
    {
        ss << " Error: " << m_reply.error().message();
    }

    handle_error(ss.str(), mxs::ErrorType::PERMANENT);
}

void PgBackendConnection::send_backlog()
{
    mxb_assert(m_state == State::ROUTING);
    auto packets = std::move(m_backlog);
    m_backlog.clear();

    for (auto it = packets.begin(); it != packets.end(); ++it)
    {
        MXB_INFO("Routing packet from backlog: %s", pg::describe(*it).c_str());

        if (!routeQuery(std::move(*it)))
        {
            handle_error("Failed to process delayed packets");
            break;
        }
        else if (m_state != State::ROUTING)
        {
            // Something caused a state to be entered that prevents further routing of packets. Wait for that
            // to finish before proceeding with the rest of the backlog.
            mxb_assert(m_backlog.empty());
            packets.erase(packets.begin(), it + 1);
            m_backlog = std::move(packets);
            break;
        }
    }
}

GWBUF PgBackendConnection::read_complete_packets()
{
    GWBUF complete_packets;

    if (auto [ok, buf] = m_dcb->read(pg::HEADER_LEN, 0); ok)
    {
        if (buf)
        {
            complete_packets = process_packets(buf);

            if (buf)
            {
                // Leftover data, either partial packets or a part of another result. Push it back into the
                // DCB and read it on the next loop. If another result is expected, the check done after
                // clientReply will cause this function to be called again.
                m_dcb->unread(std::move(buf));
            }
        }
        else
        {
            // Not even the packet header could be read
        }
    }
    else
    {
        handle_error("Network read failed");
    }

    return complete_packets;
}

bool PgBackendConnection::handle_routing()
{
    bool keep_going = false;

    if (GWBUF complete_packets = read_complete_packets())
    {
        mxs::ReplyRoute down;
        bool reply_ok = m_upstream->clientReply(std::move(complete_packets), down, m_reply);

        if (!reply_ok)
        {
            MXB_INFO("Routing the reply from '%s' failed, closing session.", m_dcb->server()->name());
            m_session->kill();
        }
        else if (!m_dcb->is_open())
        {
            // The DCB was closed as a result of the clientReply call
        }
        else if (m_reply.is_complete())
        {
            if (!m_subscriber->add_response(!m_reply.error()))
            {
                history_mismatch();
            }
            else
            {
                // If another command was executed, try to route a response again
                keep_going = track_next_result();
            }
        }
    }

    return keep_going;
}

bool PgBackendConnection::handle_reuse()
{
    if (GWBUF complete_packets = read_complete_packets())
    {
        if (m_reply.is_complete())
        {
            MXB_SINFO("Connection reset complete: " << m_reply.describe());

            if (m_reply.error())
            {
                handle_error("Failed to reuse connection: " + m_reply.error().message());
            }
            else
            {
                m_state = State::ROUTING;
                send_backlog();
            }
        }
    }

    return false;
}

bool PgBackendConnection::handle_ping()
{
    if (GWBUF complete_packets = read_complete_packets(); m_reply.is_complete())
    {
        m_state = State::ROUTING;
        send_backlog();
    }

    return false;
}

GWBUF PgBackendConnection::process_packets(GWBUF& buffer)
{
    mxb_assert(!m_reply.is_complete());
    mxb_assert(buffer.length() >= pg::HEADER_LEN);
    size_t size = 0;
    auto it = buffer.begin();

    do
    {
        if (it + pg::HEADER_LEN > buffer.end())
        {
            // Partial packet header
            break;
        }

        uint8_t command = *it;
        uint32_t len = pg::get_uint32(it + 1);

        if (it + len + 1 > buffer.end())
        {
            // Complete header but partial payload
            break;
        }

        switch (command)
        {
        case pg::ERROR_RESPONSE:
            {
                auto values = pg::extract_response_fields(it, len + 1);
                std::string_view sqlstate = values['C'];
                std::string_view errmsg = values['M'];
                m_reply.set_error(1, sqlstate.begin(), sqlstate.end(), errmsg.begin(), errmsg.end());
            }
            break;

        case pg::NOTICE_RESPONSE:
            m_reply.set_num_warnings(1);
            break;

        case pg::READY_FOR_QUERY:
            mxb_assert(len == 5);
            m_reply.set_variable(pg::TRX_STATE_VARIABLE,
                                 std::string_view {reinterpret_cast<const char*>(it + pg::HEADER_LEN), 1});

            // Result complete, the next result will be delivered in a separate clientReply call.
            m_reply.set_reply_state(mxs::ReplyState::DONE);

            // No rows and no errors means it's an "OK response"
            if (m_reply.rows_read() == 0 && !m_reply.error())
            {
                m_reply.set_is_ok(true);
            }
            break;

        case pg::DATA_ROW:
            m_reply.set_reply_state(mxs::ReplyState::RSET_ROWS);
            m_reply.add_rows(1);
            break;

        case pg::ROW_DESCRIPTION:
            m_reply.set_reply_state(mxs::ReplyState::RSET_COLDEF);
            m_reply.add_field_count(pg::get_uint16(it + pg::HEADER_LEN));
            break;

        case pg::COPY_IN_RESPONSE:
            m_reply.set_reply_state(mxs::ReplyState::LOAD_DATA);
            break;

        default:
            MXB_SDEBUG("Result command '" << command << "' not handled");
            break;
        }

        size += len + 1;
        it += len + 1;
    }
    while (!m_reply.is_complete());

    m_reply.add_bytes(size);

    mxb_assert(size <= buffer.length());
    return buffer.split(size);
}
