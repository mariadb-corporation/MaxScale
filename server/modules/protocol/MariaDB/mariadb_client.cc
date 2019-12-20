/*
 *
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/module_names.hh>
#define MXS_MODULE_NAME MXS_MARIADB_PROTOCOL_NAME

#include <maxscale/protocol/mariadb/client_connection.hh>

#include <inttypes.h>
#include <limits.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>

#include <maxbase/alloc.h>
#include <maxsql/mariadb.hh>
#include <maxscale/listener.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/backend_connection.hh>
#include <maxscale/protocol/mariadb/local_client.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/router.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/session.hh>
#include <maxscale/ssl.hh>
#include <maxscale/utils.h>
#include <maxbase/format.hh>

#include "setparser.hh"
#include "sqlmodeparser.hh"
#include "user_data.hh"

namespace
{
using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using ExcRes = mariadb::ClientAuthenticator::ExchRes;
using SUserEntry = std::unique_ptr<mariadb::UserEntry>;

const char WORD_KILL[] = "KILL";
const int CLIENT_CAPABILITIES_LEN = 32;
const int SSL_REQUEST_PACKET_SIZE = MYSQL_HEADER_LEN + CLIENT_CAPABILITIES_LEN;
const int NORMAL_HS_RESP_MIN_SIZE = MYSQL_AUTH_PACKET_BASE_SIZE + 2;
const int MAX_PACKET_SIZE = MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN;

std::string get_version_string(SERVICE* service)
{
    std::string rval = service->version_string();
    if (rval.empty())
    {
        rval = DEFAULT_VERSION_STRING;
    }

    // Older applications don't understand versions other than 5 and cause strange problems.
    // TODO: Is this still necessary?
    if (rval[0] != '5')
    {
        const char prefix[] = "5.5.5-";
        rval = prefix + rval;
    }
    return rval;
}

bool supports_extended_caps(SERVICE* service)
{
    bool rval = false;

    for (SERVER* s : service->reachable_servers())
    {
        if (s->version().total >= 100200)
        {
            rval = true;
            break;
        }
    }

    return rval;
}

uint32_t parse_packet_length(GWBUF* buffer)
{
    uint32_t prot_packet_len = 0;
    if (GWBUF_LENGTH(buffer) >= MYSQL_HEADER_LEN)
    {
        // Header in first chunk.
        prot_packet_len = MYSQL_GET_PACKET_LEN(buffer);
    }
    else
    {
        // The header is split between multiple chunks.
        uint8_t header[MYSQL_HEADER_LEN];
        gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, header);
        prot_packet_len = gw_mysql_get_byte3(header) + MYSQL_HEADER_LEN;
    }
    return prot_packet_len;
}
}

// Servers and queries to execute on them
typedef std::map<SERVER*, std::string> TargetList;

struct KillInfo
{
    typedef  bool (* DcbCallback)(DCB* dcb, void* data);

    KillInfo(std::string query, MXS_SESSION* ses, DcbCallback callback)
        : origin(mxs_rworker_get_current_id())
        , session(ses)
        , query_base(query)
        , cb(callback)
    {
    }

    int          origin;
    MXS_SESSION* session;
    std::string  query_base;
    DcbCallback  cb;
    TargetList   targets;
    std::mutex   lock;
};

static bool kill_func(DCB* dcb, void* data);

struct ConnKillInfo : public KillInfo
{
    ConnKillInfo(uint64_t id, std::string query, MXS_SESSION* ses, uint64_t keep_thread_id)
        : KillInfo(query, ses, kill_func)
        , target_id(id)
        , keep_thread_id(keep_thread_id)
    {
    }

    uint64_t target_id;
    uint64_t keep_thread_id;
};

static bool kill_user_func(DCB* dcb, void* data);

struct UserKillInfo : public KillInfo
{
    UserKillInfo(std::string name, std::string query, MXS_SESSION* ses)
        : KillInfo(query, ses, kill_user_func)
        , user(name)
    {
    }

    std::string user;
};

static bool kill_func(DCB* dcb, void* data)
{
    ConnKillInfo* info = static_cast<ConnKillInfo*>(data);

    if (dcb->session()->id() == info->target_id && dcb->role() == DCB::Role::BACKEND)
    {
        auto proto = static_cast<MariaDBBackendConnection*>(dcb->protocol());
        uint64_t backend_thread_id = proto->thread_id();

        if (info->keep_thread_id == 0 || backend_thread_id != info->keep_thread_id)
        {
            if (backend_thread_id)
            {
                // TODO: Isn't it from the context clear that dcb is a backend dcb, that is
                // TODO: perhaps that could be in the function prototype?
                BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);

                // DCB is connected and we know the thread ID so we can kill it
                std::stringstream ss;
                ss << info->query_base << backend_thread_id;

                std::lock_guard<std::mutex> guard(info->lock);
                info->targets[backend_dcb->server()] = ss.str();
            }
            else
            {
                // DCB is not yet connected, send a hangup to forcibly close it
                dcb->session()->close_reason = SESSION_CLOSE_KILLED;
                dcb->trigger_hangup_event();
            }
        }
    }

    return true;
}

static bool kill_user_func(DCB* dcb, void* data)
{
    UserKillInfo* info = (UserKillInfo*)data;

    if (dcb->role() == DCB::Role::BACKEND
        && strcasecmp(dcb->session()->user().c_str(), info->user.c_str()) == 0)
    {
        // TODO: Isn't it from the context clear that dcb is a backend dcb, that is
        // TODO: perhaps that could be in the function prototype?
        BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);

        std::lock_guard<std::mutex> guard(info->lock);
        info->targets[backend_dcb->server()] = info->query_base;
    }

    return true;
}

MariaDBClientConnection::SSLState MariaDBClientConnection::ssl_authenticate_check_status()
{
    /**
     * We record the SSL status before and after ssl authentication. This allows
     * us to detect if the SSL handshake is immediately completed, which means more
     * data needs to be read from the socket.
     */
    bool health_before = (m_dcb->ssl_state() == DCB::SSLState::ESTABLISHED);
    int ssl_ret = ssl_authenticate_client();
    bool health_after = (m_dcb->ssl_state() == DCB::SSLState::ESTABLISHED);

    auto rval = SSLState::FAIL;
    if (ssl_ret != 0)
    {
        rval = (ssl_ret == SSL_ERROR_CLIENT_NOT_SSL) ? SSLState::NOT_CAPABLE : SSLState::FAIL;
    }
    else if (!health_after)
    {
        rval = SSLState::INCOMPLETE;
    }
    else if (!health_before && health_after)
    {
        rval = SSLState::INCOMPLETE;
        m_dcb->trigger_read_event();
    }
    else if (health_before && health_after)
    {
        rval = SSLState::COMPLETE;
    }
    return rval;
}

/**
 * Start or continue ssl handshake. If the listener requires SSL but the client is not SSL capable,
 * an error message is recorded and failure return given.
 *
 * @return 0 if ok, >0 if a problem - see return codes defined in ssl.h
 */
int MariaDBClientConnection::ssl_authenticate_client()
{
    auto dcb = m_dcb;

    const char* remote = m_dcb->remote().c_str();
    const char* service = m_session->service->name();

    /* Now we require an SSL connection */
    if (!m_session_data->ssl_capable())
    {
        /* Should be SSL, but client is not SSL capable. Cannot print the username, as client has not
         * sent that yet. */
        MXS_INFO("Client from '%s' attempted to connect to service '%s' without SSL when SSL was required.",
                 remote, service);
        return SSL_ERROR_CLIENT_NOT_SSL;
    }

    /* Now we know SSL is required and client is capable */
    if (m_dcb->ssl_state() != DCB::SSLState::ESTABLISHED)
    {
        int return_code;
        /** Do the SSL Handshake */
        if (m_dcb->ssl_state() == DCB::SSLState::HANDSHAKE_UNKNOWN)
        {
            m_dcb->set_ssl_state(DCB::SSLState::HANDSHAKE_REQUIRED);
        }
        /**
         * Note that this will often fail to achieve its result, because further
         * reading (or possibly writing) of SSL related information is needed.
         * When that happens, there is a call in poll.c so that an EPOLLIN
         * event that arrives while the SSL state is SSL_HANDSHAKE_REQUIRED
         * will trigger DCB::ssl_handshake. This situation does not result in a
         * negative return code - that indicates a real failure.
         */
        return_code = dcb->ssl_handshake();
        if (return_code < 0)
        {
            MXS_INFO("Client from '%s' failed to connect to service '%s' with SSL.",
                     remote, service);
            return SSL_ERROR_ACCEPT_FAILED;
        }
        else if (mxs_log_is_priority_enabled(LOG_INFO))
        {
            if (return_code == 1)
            {
                MXS_INFO("Client from '%s' connected to service '%s' with SSL.",
                         remote, service);
            }
            else
            {
                MXS_INFO("Client from '%s' is in progress of connecting to service '%s' with SSL.",
                         remote, service);
            }
        }
    }
    return SSL_AUTH_CHECKS_OK;
}

/**
 * Send the server handshake packet to the client.
 *
 * @return The packet length sent
 */
int MariaDBClientConnection::send_mysql_client_handshake()
{
    auto service = m_session->service;

    uint8_t mysql_packet_header[4];
    uint8_t mysql_packet_id = 0;
    /* uint8_t mysql_filler = GW_MYSQL_HANDSHAKE_FILLER; not needed*/
    uint8_t mysql_protocol_version = GW_MYSQL_PROTOCOL_VERSION;
    uint8_t* mysql_handshake_payload = NULL;
    uint8_t mysql_thread_id_num[4];
    uint8_t mysql_scramble_buf[9] = "";
    uint8_t mysql_plugin_data[13] = "";
    uint8_t mysql_server_capabilities_one[2];
    uint8_t mysql_server_capabilities_two[2];
    uint8_t mysql_server_language = service->charset();
    if (mysql_server_language == 0)
    {
        // Charset 8 is latin1, the server default.
        mysql_server_language = 8;
    }

    uint8_t mysql_server_status[2];
    uint8_t mysql_scramble_len = 21;
    uint8_t mysql_filler_ten[10] = {};
    /* uint8_t mysql_last_byte = 0x00; not needed */
    char server_scramble[GW_MYSQL_SCRAMBLE_SIZE + 1] = "";
    bool is_maria = supports_extended_caps(service);

    gw_generate_random_str(server_scramble, GW_MYSQL_SCRAMBLE_SIZE);

    // copy back to the caller
    memcpy(m_session_data->scramble, server_scramble, GW_MYSQL_SCRAMBLE_SIZE);

    if (is_maria)
    {
        /**
         * The new 10.2 capability flags are stored in the last 4 bytes of the
         * 10 byte filler block.
         */
        uint32_t new_flags = MXS_MARIA_CAP_STMT_BULK_OPERATIONS;
        memcpy(mysql_filler_ten + 6, &new_flags, sizeof(new_flags));
    }

    // Send the session id as the server thread id. Only the low 32bits are sent in the handshake.
    auto thread_id = m_session->id();
    gw_mysql_set_byte4(mysql_thread_id_num, (uint32_t)(thread_id));
    memcpy(mysql_scramble_buf, server_scramble, 8);

    memcpy(mysql_plugin_data, server_scramble + 8, 12);

    /**
     * Use the default authentication plugin name in the initial handshake. If the
     * authenticator needs to change the authentication method, it should send
     * an AuthSwitchRequest packet to the client.
     */
    const char* plugin_name = DEFAULT_MYSQL_AUTH_PLUGIN;
    int plugin_name_len = strlen(plugin_name);

    std::string version = get_version_string(service);

    uint32_t mysql_payload_size =
        sizeof(mysql_protocol_version) + (version.length() + 1) + sizeof(mysql_thread_id_num) + 8
        + sizeof(    /* mysql_filler */ uint8_t) + sizeof(mysql_server_capabilities_one)
        + sizeof(mysql_server_language)
        + sizeof(mysql_server_status) + sizeof(mysql_server_capabilities_two) + sizeof(mysql_scramble_len)
        + sizeof(mysql_filler_ten) + 12 + sizeof(    /* mysql_last_byte */ uint8_t) + plugin_name_len
        + sizeof(    /* mysql_last_byte */ uint8_t);

    // allocate memory for packet header + payload
    GWBUF* buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
    if (!buf)
    {
        mxb_assert(buf);
        return 0;
    }
    uint8_t* outbuf = GWBUF_DATA(buf);

    // write packet header with mysql_payload_size
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);

    // write packet number, now is 0
    mysql_packet_header[3] = mysql_packet_id;
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    // current buffer pointer
    mysql_handshake_payload = outbuf + sizeof(mysql_packet_header);

    // write protocol version
    memcpy(mysql_handshake_payload, &mysql_protocol_version, sizeof(mysql_protocol_version));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_protocol_version);

    // write server version plus 0 filler
    strcpy((char*)mysql_handshake_payload, version.c_str());
    mysql_handshake_payload = mysql_handshake_payload + version.length();

    *mysql_handshake_payload = 0x00;

    mysql_handshake_payload++;

    // write thread id
    memcpy(mysql_handshake_payload, mysql_thread_id_num, sizeof(mysql_thread_id_num));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_thread_id_num);

    // write scramble buf
    memcpy(mysql_handshake_payload, mysql_scramble_buf, 8);
    mysql_handshake_payload = mysql_handshake_payload + 8;
    *mysql_handshake_payload = GW_MYSQL_HANDSHAKE_FILLER;
    mysql_handshake_payload++;

    // write server capabilities part one
    mysql_server_capabilities_one[0] = (uint8_t)GW_MYSQL_CAPABILITIES_SERVER;
    mysql_server_capabilities_one[1] = (uint8_t)(GW_MYSQL_CAPABILITIES_SERVER >> 8);

    if (is_maria)
    {
        /** A MariaDB 10.2 server doesn't send the CLIENT_MYSQL capability
         * to signal that it supports extended capabilities */
        mysql_server_capabilities_one[0] &= ~(uint8_t)GW_MYSQL_CAPABILITIES_CLIENT_MYSQL;
    }

    if (require_ssl())
    {
        mysql_server_capabilities_one[1] |= (int)GW_MYSQL_CAPABILITIES_SSL >> 8;
    }

    memcpy(mysql_handshake_payload, mysql_server_capabilities_one, sizeof(mysql_server_capabilities_one));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_capabilities_one);

    // write server language
    memcpy(mysql_handshake_payload, &mysql_server_language, sizeof(mysql_server_language));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_language);

    // write server status
    mysql_server_status[0] = 2;
    mysql_server_status[1] = 0;
    memcpy(mysql_handshake_payload, mysql_server_status, sizeof(mysql_server_status));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_status);

    // write server capabilities part two
    mysql_server_capabilities_two[0] = (uint8_t)(GW_MYSQL_CAPABILITIES_SERVER >> 16);
    mysql_server_capabilities_two[1] = (uint8_t)(GW_MYSQL_CAPABILITIES_SERVER >> 24);

    /** NOTE: pre-2.1 versions sent the fourth byte of the capabilities as
     *  the value 128 even though there's no such capability. */

    memcpy(mysql_handshake_payload, mysql_server_capabilities_two, sizeof(mysql_server_capabilities_two));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_capabilities_two);

    // write scramble_len
    memcpy(mysql_handshake_payload, &mysql_scramble_len, sizeof(mysql_scramble_len));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_scramble_len);

    // write 10 filler
    memcpy(mysql_handshake_payload, mysql_filler_ten, sizeof(mysql_filler_ten));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_filler_ten);

    // write plugin data
    memcpy(mysql_handshake_payload, mysql_plugin_data, 12);
    mysql_handshake_payload = mysql_handshake_payload + 12;

    // write last byte, 0
    *mysql_handshake_payload = 0x00;
    mysql_handshake_payload++;

    // to be understanded ????
    memcpy(mysql_handshake_payload, plugin_name, plugin_name_len);
    mysql_handshake_payload = mysql_handshake_payload + plugin_name_len;

    // write last byte, 0
    *mysql_handshake_payload = 0x00;

    // writing data in the Client buffer queue
    write(buf);

    m_state = State::AUTHENTICATING;
    return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * @brief Analyse authentication errors and write appropriate log messages
 *
 * @param dcb Request handler DCB connected to the client
 * @param auth_val The type of authentication failure
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 */
void MariaDBClientConnection::handle_authentication_errors(DCB* generic_dcb, AuthRes auth_val,
                                                           int packet_number)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);

    char* fail_str = NULL;
    MYSQL_session* session = m_session_data;

    switch (auth_val)
    {
    case AuthRes::FAIL:
        MXS_DEBUG("authentication failed. fd %d, state = MYSQL_FAILED_AUTH.", dcb->fd());
        /** Send error 1045 to client */
        fail_str = create_auth_fail_str(session->user.c_str(),
                                        dcb->remote().c_str(),
                                        !session->auth_token.empty(),
                                        session->db.c_str(),
                                        auth_val);
        modutil_send_mysql_err_packet(dcb, packet_number, 0, 1045, "28000", fail_str);
        break;

    default:
        MXS_DEBUG("authentication failed. fd %d, state unrecognized.", dcb->fd());
        /** Send error 1045 to client */
        fail_str = create_auth_fail_str(session->user.c_str(),
                                        dcb->remote().c_str(),
                                        !session->auth_token.empty(),
                                        session->db.c_str(),
                                        auth_val);
        modutil_send_mysql_err_packet(dcb, packet_number, 0, 1045, "28000", fail_str);
    }
    MXS_FREE(fail_str);
}

/**
 * Start or continue authenticating the client.
 *
 * @return True if authentication is still ongoing or complete, false if authentication failed and the
 * connection should be closed.
 */
bool MariaDBClientConnection::perform_authentication()
{
    // The first response from client requires special handling.
    mxs::Buffer read_buffer;
    bool read_success = (m_auth_state == AuthState::INIT) ? read_first_client_packet(&read_buffer) :
        read_protocol_packet(&read_buffer);
    if (!read_success)
    {
        return false;
    }
    else if (read_buffer.empty())
    {
        // Not enough data was available yet.
        return true;
    }

    auto buffer = read_buffer.release();
    // Save next sequence to session. Authenticator may use the value.
    uint8_t sequence = 0;
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &sequence);
    auto next_seq = sequence + 1;
    m_session_data->next_sequence = next_seq;

    const char wrong_sequence[] = "Client (%s) sent packet with unexpected sequence number. "
                                  "Expected %i, got %i.";
    const char packets_ooo[] = "Got packets out of order";
    const char sql_errstate[] = "08S01";
    const int er_bad_handshake = 1043;
    const int er_out_of_order = 1156;

    auto send_access_denied_error = [this, &next_seq] ()
    {
        std::string msg = mxb::string_printf("Access denied for user '%s'@'%s' (using password: %s)",
            m_session_data->user.c_str(), m_session_data->remote.c_str(),
            m_session_data->auth_token.empty() ? "NO" : "YES");
        modutil_send_mysql_err_packet(m_dcb, next_seq, 0, 1045, "2800", msg.c_str());
    };

    auto remote = m_dcb->remote().c_str();
    bool error = false;
    bool state_machine_continue = true;
    while (state_machine_continue)
    {
        switch (m_auth_state)
        {
        case AuthState::INIT:
            m_auth_state = require_ssl() ? AuthState::EXPECT_SSL_REQ : AuthState::EXPECT_HS_RESP;
            break;

        case AuthState::EXPECT_SSL_REQ:
            {
                // Expecting SSLRequest
                if (sequence == 1)
                {
                    if (parse_ssl_request_packet(buffer))
                    {
                        m_auth_state = AuthState::SSL_NEG;
                    }
                    else
                    {
                        modutil_send_mysql_err_packet(
                            m_dcb, next_seq, 0, er_bad_handshake, sql_errstate, "Bad SSL handshake");
                        MXB_ERROR("Client (%s) sent an invalid SSLRequest.", remote);
                        m_auth_state = AuthState::FAIL;
                    }
                }
                else
                {
                    modutil_send_mysql_err_packet(
                        m_dcb, next_seq, 0, er_out_of_order, sql_errstate, packets_ooo);
                    MXB_ERROR(wrong_sequence, m_session_data->remote.c_str(), 1, sequence);
                    m_auth_state = AuthState::FAIL;
                }
            }
            break;

        case AuthState::SSL_NEG:
            {
                // Client should be negotiating ssl.
                auto ssl_status = ssl_authenticate_check_status();
                if (ssl_status == SSLState::COMPLETE)
                {
                    m_auth_state = AuthState::EXPECT_HS_RESP;
                }
                else if (ssl_status == SSLState::INCOMPLETE)
                {
                    // SSL negotiation should complete in the background. Execution returns here once
                    // complete.
                    state_machine_continue = false;
                }
                else
                {
                    mysql_send_auth_error(m_dcb, next_seq, "Access without SSL denied");
                    MXB_ERROR("Client (%s) failed SSL negotiation.", m_session_data->remote.c_str());
                    m_auth_state = AuthState::FAIL;
                }
            }
            break;

        case AuthState::EXPECT_HS_RESP:
            {
                // Expecting normal Handshake response
                // @see https://mariadb.com/kb/en/library/connection/#client-handshake-response
                int expected_seq = require_ssl() ? 2 : 1;
                if (sequence == expected_seq)
                {
                    if (parse_handshake_response_packet(buffer))
                    {
                        m_auth_state = AuthState::PREPARE_AUTH;
                    }
                    else
                    {
                        modutil_send_mysql_err_packet(
                            m_dcb, next_seq, 0, er_bad_handshake, sql_errstate, "Bad handshake");
                        MXB_ERROR("Client (%s) sent an invalid HandShakeResponse.",
                                  m_session_data->remote.c_str());
                        m_auth_state = AuthState::FAIL;
                    }
                }
                else
                {
                    modutil_send_mysql_err_packet(
                        m_dcb, next_seq, 0, er_out_of_order, sql_errstate, packets_ooo);
                    MXB_ERROR(wrong_sequence, m_session_data->remote.c_str(), expected_seq, sequence);
                    m_auth_state = AuthState::FAIL;
                }
            }
            break;

        case AuthState::PREPARE_AUTH:
            if (prepare_authentication())
            {
                m_auth_state = AuthState::ASK_FOR_TOKEN;
            }
            else
            {
                send_access_denied_error();
                m_auth_state = AuthState::FAIL;
            }
            break;

        case AuthState::ASK_FOR_TOKEN:
            {
                mxs::Buffer auth_output;
                auto auth_val = m_authenticator->exchange(buffer, m_session_data, &auth_output);
                if (!auth_output.empty())
                {
                    write(auth_output.release());
                }

                if (auth_val == ExcRes::READY)
                {
                    // Continue to password check.
                    m_auth_state = AuthState::CHECK_TOKEN;
                }
                else if (auth_val == ExcRes::INCOMPLETE)
                {
                    // Authentication is expecting another packet from client, so jump out.
                    state_machine_continue = false;
                }
                else
                {
                    // Authentication failed. TODO: is this the correct error to send?
                    send_access_denied_error();
                    mxs::mark_auth_as_failed(m_dcb->remote());
                    m_auth_state = AuthState::FAIL;
                }
            }
            break;

        case AuthState::CHECK_TOKEN:
            {
                auto auth_val = m_authenticator->authenticate(m_dcb, m_user_entry.get(), m_session_data);
                if (auth_val == AuthRes::SUCCESS)
                {
                    m_auth_state = AuthState::START_SESSION;
                }
                else
                {
                    if (auth_val == AuthRes::FAIL_WRONG_PW)
                    {
                        // Again, this may be because user data is obsolete.
                        m_session->service->notify_authentication_failed();
                    }
                    send_access_denied_error();
                    m_auth_state = AuthState::FAIL;
                }
            }
            break;

        case AuthState::START_SESSION:
            // Authentication success, initialize session.
            m_session->set_user(m_session_data->user);
            if (session_start(m_session))
            {
                mxb_assert(m_session->state() != MXS_SESSION::State::CREATED);
                m_auth_state = AuthState::COMPLETE;
            }
            else
            {
                mysql_send_auth_error(m_dcb, next_seq,
                                      "Session creation failed, MaxScale may be out of memory");
                MXB_ERROR("Failed to create session for '%s'@'%s'.", "a", "b");
                m_auth_state = AuthState::FAIL;
            }
            break;

        case AuthState::COMPLETE:
            m_sql_mode = m_session->listener_data()->m_default_sql_mode;
            mxs_mysql_send_ok(m_dcb, m_session_data->next_sequence, 0, NULL);
            if (m_dcb->readq())
            {
                // The user has already sent more data, process it
                m_dcb->trigger_read_event();
            }
            m_state = State::READY;
            state_machine_continue = false;
            break;

        case AuthState::FAIL:
            // Close DCB. Will release session. An error message should have already been sent.
            m_state = State::FAILED;
            state_machine_continue = false;
            error = true;
            break;
        }
    }

    /* One way or another, the buffer is now fully processed */
    gwbuf_free(buffer);
    return !error;
}

bool MariaDBClientConnection::prepare_authentication()
{
    auto search_settings = user_search_settings();
    // The correct authenticator is chosen here (and also in reauthenticate_client()).
    auto users = user_account_cache();
    auto entry = users->find_user(m_session_data->user, m_session_data->remote, m_session_data->db,
                                  search_settings);

    bool found_good_entry = false;
    if (entry)
    {
        mariadb::AuthenticatorModule* selected_module = nullptr;
        auto& auth_modules = *(m_session_data->allowed_authenticators);
        for (const auto& auth_module : auth_modules)
        {
            if (auth_module->supported_plugins().count(entry->plugin))
            {
                // Found correct authenticator for the user entry.
                selected_module = auth_module.get();
                break;
            }
        }

        if (selected_module)
        {
            // Save related data so that later calls do not need to perform the same work.
            m_user_entry = std::move(entry);
            m_session_data->m_current_authenticator = selected_module;
            m_authenticator = selected_module->create_client_authenticator();
            found_good_entry = true;
        }
        else
        {
            MXB_INFO("User entry '%s@'%s' uses unrecognized authenticator plugin '%s'. "
                     "Cannot authenticate user.",
                     entry->username.c_str(), entry->host_pattern.c_str(), entry->plugin.c_str());
        }
    }

    if (!found_good_entry)
    {
        // User data may be outdated, send update message through the service. The current session
        // will fail.
        m_session->service->notify_authentication_failed();
    }
    return found_good_entry && m_authenticator;
}

/**
 * Handle relevant variables
 *
 * @param session      The session for which the query classifier mode is adjusted.
 * @param read_buffer  Pointer to a buffer, assumed to contain a statement.
 *                     May be reallocated if not contiguous.
 *
 * @return NULL if successful, otherwise dynamically allocated error message.
 */
char* MariaDBClientConnection::handle_variables(MXS_SESSION* session, GWBUF** read_buffer)
{
    char* message = NULL;

    SetParser set_parser;
    SetParser::Result result;

    switch (set_parser.check(read_buffer, &result))
    {
    case SetParser::ERROR:
        // In practice only OOM.
        break;

    case SetParser::IS_SET_SQL_MODE:
        {
            SqlModeParser sql_mode_parser;

            const SetParser::Result::Items& values = result.values();

            for (SetParser::Result::Items::const_iterator i = values.begin(); i != values.end(); ++i)
            {
                const SetParser::Result::Item& value = *i;

                switch (sql_mode_parser.get_sql_mode(value.first, value.second))
                {
                case SqlModeParser::ORACLE:
                    session->set_autocommit(false);
                    m_sql_mode = QC_SQL_MODE_ORACLE;
                    break;

                case SqlModeParser::DEFAULT:
                    session->set_autocommit(true);
                    m_sql_mode = QC_SQL_MODE_DEFAULT;
                    break;

                case SqlModeParser::SOMETHING:
                    break;

                default:
                    mxb_assert(!true);
                }
            }
        }
        break;

    case SetParser::IS_SET_MAXSCALE:
        {
            const SetParser::Result::Items& variables = result.variables();
            const SetParser::Result::Items& values = result.values();

            SetParser::Result::Items::const_iterator i = variables.begin();
            SetParser::Result::Items::const_iterator j = values.begin();

            while (!message && (i != variables.end()))
            {
                const SetParser::Result::Item& variable = *i;
                const SetParser::Result::Item& value = *j;

                message = session_set_variable_value(session,
                                                     variable.first,
                                                     variable.second,
                                                     value.first,
                                                     value.second);

                ++i;
                ++j;
            }
        }
        break;

    case SetParser::NOT_RELEVANT:
        break;

    default:
        mxb_assert(!true);
    }

    return message;
}

/**
 * Perform re-authentication of the client
 *
 * @param session   Client session
 * @param packetbuf Client's response to the AuthSwitchRequest
 *
 * @return True if the user is allowed access
 */
bool MariaDBClientConnection::reauthenticate_client(MXS_SESSION* session, GWBUF* packetbuf)
{
    bool rval = false;
    // Assume for now that reauthentication uses the same plugin, fix later.
    if (m_session_data->m_current_authenticator->capabilities()
        & mariadb::AuthenticatorModule::CAP_REAUTHENTICATE)
    {
        std::vector<uint8_t> orig_payload;
        uint32_t orig_len = m_stored_query.length();
        orig_payload.resize(orig_len);
        gwbuf_copy_data(m_stored_query.get(), 0, orig_len, orig_payload.data());

        auto it = orig_payload.begin();
        it += MYSQL_HEADER_LEN + 1;     // Skip header and command byte
        auto user_end = std::find(it, orig_payload.end(), '\0');

        if (user_end == orig_payload.end())
        {
            mysql_send_auth_error(m_dcb, 3, "Malformed AuthSwitchRequest packet");
            return false;
        }

        std::string user(it, user_end);
        it = user_end;
        ++it;

        // Skip the auth token
        auto token_len = *it++;
        it += token_len;

        auto db_end = std::find(it, orig_payload.end(), '\0');

        if (db_end == orig_payload.end())
        {
            mysql_send_auth_error(m_dcb, 3, "Malformed AuthSwitchRequest packet");
            return false;
        }

        std::string db(it, db_end);

        it = db_end;
        ++it;

        unsigned int client_charset = *it++;
        client_charset |= ((unsigned int)(*it++) << 8u);
        m_session_data->client_info.m_charset = client_charset;

        // Copy the new username to the session data
        MYSQL_session* data = m_session_data;
        data->user = user;
        data->db = db;

        auto users = user_account_cache();
        auto search_settings = user_search_settings();
        auto user_entry = users->find_user(data->user, data->remote, data->db, search_settings);

        auto rc = AuthRes::FAIL;
        if (user_entry)
        {
            std::vector<uint8_t> payload;
            uint64_t payloadlen = gwbuf_length(packetbuf) - MYSQL_HEADER_LEN;
            payload.resize(payloadlen);
            gwbuf_copy_data(packetbuf, MYSQL_HEADER_LEN, payloadlen, &payload[0]);

            rc = m_authenticator->reauthenticate(
                user_entry.get(), m_dcb, data->scramble, sizeof(data->scramble), payload, data->client_sha1);
            if (rc == AuthRes::SUCCESS)
            {
                // Re-authentication successful, route the original COM_CHANGE_USER
                rval = true;
            }
        }

        if (!rval)
        {
            /**
             * Authentication failed. To prevent the COM_CHANGE_USER from reaching
             * the backend servers (and possibly causing problems) the client
             * connection will be closed.
             *
             * First packet is COM_CHANGE_USER, the second is AuthSwitchRequest,
             * third is the response and the fourth is the following error.
             */
            handle_authentication_errors(m_dcb, rc, 3);
        }
    }

    return rval;
}

void MariaDBClientConnection::track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf)
{
    mxb_assert(GWBUF_IS_CONTIGUOUS(packetbuf));
    mxb_assert((session->get_trx_state() & (SESSION_TRX_STARTING | SESSION_TRX_ENDING))
               != (SESSION_TRX_STARTING | SESSION_TRX_ENDING));

    if (session->is_trx_ending())
    {
        if (session->is_autocommit())
        {
            // Transaction ended, go into inactive state
            session->set_trx_state(SESSION_TRX_INACTIVE);
        }
        else
        {
            // Without autocommit the end of a transaction starts a new one
            session->set_trx_state(SESSION_TRX_ACTIVE | SESSION_TRX_STARTING);
        }
    }
    else if (session->is_trx_starting())
    {
        uint32_t trx_state = session->get_trx_state();
        trx_state &= ~SESSION_TRX_STARTING;
        session->set_trx_state(trx_state);
    }
    else if (!session->is_autocommit() && session->get_trx_state() == SESSION_TRX_INACTIVE)
    {
        // This state is entered when autocommit was disabled
        session->set_trx_state(SESSION_TRX_ACTIVE | SESSION_TRX_STARTING);
    }

    if (mxs_mysql_get_command(packetbuf) == MXS_COM_QUERY)
    {
        uint32_t type = qc_get_trx_type_mask(packetbuf);

        if (type & QUERY_TYPE_BEGIN_TRX)
        {
            if (type & QUERY_TYPE_DISABLE_AUTOCOMMIT)
            {
                // This disables autocommit and the next statement starts a new transaction
                session->set_autocommit(false);
                session->set_trx_state(SESSION_TRX_INACTIVE);
            }
            else
            {
                uint32_t trx_state = SESSION_TRX_ACTIVE | SESSION_TRX_STARTING;

                if (type & QUERY_TYPE_READ)
                {
                    trx_state |= SESSION_TRX_READ_ONLY;
                }

                session->set_trx_state(trx_state);
            }
        }
        else if (type & (QUERY_TYPE_COMMIT | QUERY_TYPE_ROLLBACK))
        {
            uint32_t trx_state = session->get_trx_state();
            trx_state |= SESSION_TRX_ENDING;
            // A commit never starts a new transaction. This would happen with: SET AUTOCOMMIT=0; COMMIT;
            trx_state &= ~SESSION_TRX_STARTING;
            session->set_trx_state(trx_state);

            if (type & QUERY_TYPE_ENABLE_AUTOCOMMIT)
            {
                session->set_autocommit(true);
            }
        }
    }
}

bool MariaDBClientConnection::handle_change_user(bool* changed_user, GWBUF** packetbuf)
{
    bool ok = true;
    if (!m_changing_user && m_command == MXS_COM_CHANGE_USER)
    {
        // Track the COM_CHANGE_USER progress at the session level
        m_session_data->changing_user = true;

        *changed_user = true;
        send_auth_switch_request_packet();

        // Store the original COM_CHANGE_USER for later
        m_stored_query = mxs::Buffer(*packetbuf);
        *packetbuf = NULL;
    }
    else if (m_changing_user)
    {
        mxb_assert(m_command == MXS_COM_CHANGE_USER);
        m_changing_user = false;
        bool ok = reauthenticate_client(m_session, *packetbuf);
        gwbuf_free(*packetbuf);
        *packetbuf = m_stored_query.release();
    }

    return ok;
}

/**
 * Parse a "KILL [CONNECTION | QUERY] [ <process_id> | USER <username> ]" query.
 * Will modify the argument string even if unsuccessful.
 *
 * @param query Query string to parse
 * @paran thread_id_out Thread id output
 * @param kt_out Kill command type output
 * @param user_out Kill command target user output
 * @return true on success, false on error
 */
bool MariaDBClientConnection::parse_kill_query(char* query, uint64_t* thread_id_out, kill_type_t* kt_out,
                                               std::string* user_out)
{
    const char WORD_CONNECTION[] = "CONNECTION";
    const char WORD_QUERY[] = "QUERY";
    const char WORD_HARD[] = "HARD";
    const char WORD_SOFT[] = "SOFT";
    const char WORD_USER[] = "USER";
    const char DELIM[] = " \n\t";

    int kill_type = KT_CONNECTION;
    unsigned long long int thread_id = 0;
    std::string tmpuser;

    auto extract_user = [](char* token, std::string* user) {
            char* end = strchr(token, ';');

            if (end)
            {
                user->assign(token, end - token);
            }
            else
            {
                user->assign(token);
            }
        };

    enum kill_parse_state_t
    {
        KILL,
        CONN_QUERY,
        ID,
        USER,
        SEMICOLON,
        DONE
    } state = KILL;
    char* saveptr = NULL;
    bool error = false;

    char* token = strtok_r(query, DELIM, &saveptr);

    while (token && !error)
    {
        bool get_next = false;
        switch (state)
        {
        case KILL:
            if (strncasecmp(token, WORD_KILL, sizeof(WORD_KILL) - 1) == 0)
            {
                state = CONN_QUERY;
                get_next = true;
            }
            else
            {
                error = true;
            }
            break;

        case CONN_QUERY:
            if (strncasecmp(token, WORD_QUERY, sizeof(WORD_QUERY) - 1) == 0)
            {
                kill_type &= ~KT_CONNECTION;
                kill_type |= KT_QUERY;
                get_next = true;
            }
            else if (strncasecmp(token, WORD_CONNECTION, sizeof(WORD_CONNECTION) - 1) == 0)
            {
                get_next = true;
            }

            if (strncasecmp(token, WORD_HARD, sizeof(WORD_HARD) - 1) == 0)
            {
                kill_type |= KT_HARD;
                get_next = true;
            }
            else if (strncasecmp(token, WORD_SOFT, sizeof(WORD_SOFT) - 1) == 0)
            {
                kill_type |= KT_SOFT;
                get_next = true;
            }
            else
            {
                /* Move to next state regardless of comparison result. The current
                 * part is optional and the process id may already be in the token. */
                state = ID;
            }
            break;

        case ID:
            if (strncasecmp(token, WORD_USER, sizeof(WORD_USER) - 1) == 0)
            {
                state = USER;
                get_next = true;
                break;
            }
            else
            {
                char* endptr_id = NULL;

                long long int l = strtoll(token, &endptr_id, 0);

                if ((l == LLONG_MAX && errno == ERANGE)
                    || (*endptr_id != '\0' && *endptr_id != ';')
                    || l <= 0 || endptr_id == token)
                {
                    // Not a positive 32-bit integer
                    error = true;
                }
                else
                {
                    mxb_assert(*endptr_id == '\0' || *endptr_id == ';');
                    state = SEMICOLON;      // In case we have space before ;
                    get_next = true;
                    thread_id = l;
                }
            }
            break;

        case USER:
            extract_user(token, &tmpuser);
            state = SEMICOLON;
            get_next = true;
            break;

        case SEMICOLON:
            if (strncmp(token, ";", 1) == 0)
            {
                state = DONE;
                get_next = true;
            }
            else
            {
                error = true;
            }
            break;

        default:
            error = true;
            break;
        }

        if (get_next)
        {
            token = strtok_r(NULL, DELIM, &saveptr);
        }
    }

    if (error || (state != DONE && state != SEMICOLON))
    {
        return false;
    }
    else
    {
        *thread_id_out = thread_id;
        *kt_out = (kill_type_t)kill_type;
        *user_out = tmpuser;
        return true;
    }
}

/**
 * Handle text version of KILL [CONNECTION | QUERY] <process_id>. Only detects
 * commands in the beginning of the packet and with no comments.
 * Increased parsing would slow down the handling of every single query.
 *
 * @param dcb         Client dcb
 * @param read_buffer Input buffer
 * @param packet_len  Length of the protocol packet
 *
 * @return RES_CONTINUE or RES_END
 */
MariaDBClientConnection::SpecialCmdRes
MariaDBClientConnection::handle_query_kill(DCB* dcb, GWBUF* read_buffer, uint32_t packet_len)
{
    auto rval = SpecialCmdRes::CONTINUE;
    /* First, we need to detect the text "KILL" (ignorecase) in the start
     * of the packet. Copy just enough characters. */
    const size_t KILL_BEGIN_LEN = sizeof(WORD_KILL) - 1;
    char startbuf[KILL_BEGIN_LEN];      // Not 0-terminated, careful...
    size_t copied_len = gwbuf_copy_data(read_buffer,
                                        MYSQL_HEADER_LEN + 1,
                                        KILL_BEGIN_LEN,
                                        (uint8_t*)startbuf);

    if (strncasecmp(WORD_KILL, startbuf, KILL_BEGIN_LEN) == 0)
    {
        /* Good chance that the query is a KILL-query. Copy the entire
         * buffer and process. */
        size_t buffer_len = packet_len - (MYSQL_HEADER_LEN + 1);
        char querybuf[buffer_len + 1];          // 0-terminated
        copied_len = gwbuf_copy_data(read_buffer,
                                     MYSQL_HEADER_LEN + 1,
                                     buffer_len,
                                     (uint8_t*)querybuf);
        querybuf[copied_len] = '\0';
        kill_type_t kt = KT_CONNECTION;
        uint64_t thread_id = 0;
        std::string user;

        if (parse_kill_query(querybuf, &thread_id, &kt, &user))
        {
            rval = SpecialCmdRes::END;

            if (thread_id > 0)
            {
                mxs_mysql_execute_kill(dcb->session(), thread_id, kt);
            }
            else if (!user.empty())
            {
                mxs_mysql_execute_kill_user(dcb->session(), user.c_str(), kt);
            }

            mxs_mysql_send_ok(dcb, 1, 0, NULL);
        }
    }

    return rval;
}

void MariaDBClientConnection::handle_use_database(GWBUF* read_buffer)
{
    auto databases = qc_get_database_names(read_buffer);

    if (!databases.empty())
    {
        m_session_data->db = databases[0];
        m_session->start_database_change(m_session_data->db);
    }
}

/**
 * Some SQL commands/queries need to be detected and handled by the protocol
 * and MaxScale instead of being routed forward as is.
 *
 * @param dcb         Client dcb
 * @param read_buffer The current read buffer
 * @param cmd         Current command being executed
 *
 * @return see @c spec_com_res_t
 */
MariaDBClientConnection::SpecialCmdRes
MariaDBClientConnection::process_special_commands(DCB* dcb, GWBUF* read_buffer, uint8_t cmd)
{
    auto is_use_database = [](GWBUF* buffer, size_t packet_len) -> bool {
            const char USE[] = "USE ";
            char* ptr = (char*)GWBUF_DATA(buffer) + MYSQL_HEADER_LEN + 1;

            return packet_len > MYSQL_HEADER_LEN + 1 + (sizeof(USE) - 1)
                   && strncasecmp(ptr, USE, sizeof(USE) - 1) == 0;
        };

    auto is_kill_query = [](GWBUF* buffer, size_t packet_len) -> bool {
            const char KILL[] = "KILL ";
            char* ptr = (char*)GWBUF_DATA(buffer) + MYSQL_HEADER_LEN + 1;

            return packet_len > MYSQL_HEADER_LEN + 1 + (sizeof(KILL) - 1)
                   && strncasecmp(ptr, KILL, sizeof(KILL) - 1) == 0;
        };

    auto rval = SpecialCmdRes::CONTINUE;
    if (cmd == MXS_COM_QUIT)
    {
        /** The client is closing the connection. We know that this will be the
         * last command the client sends so the backend connections are very likely
         * to be in an idle state.
         *
         * If the client is pipelining the queries (i.e. sending N request as
         * a batch and then expecting N responses) then it is possible that
         * the backend connections are not idle when the COM_QUIT is received.
         * In most cases we can assume that the connections are idle. */
        session_qualify_for_pool(dcb->session());
    }
    else if (cmd == MXS_COM_SET_OPTION)
    {
        /**
         * This seems to be only used by some versions of PHP.
         *
         * The option is stored as a two byte integer with the values 0 for enabling
         * multi-statements and 1 for disabling it.
         */
        if (GWBUF_DATA(read_buffer)[MYSQL_HEADER_LEN + 2])
        {
            m_session_data->client_info.m_client_capabilities &= ~GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS;
        }
        else
        {
            m_session_data->client_info.m_client_capabilities |= GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS;
        }
    }
    else if (cmd == MXS_COM_PROCESS_KILL)
    {
        uint64_t process_id = gw_mysql_get_byte4(GWBUF_DATA(read_buffer) + MYSQL_HEADER_LEN + 1);
        mxs_mysql_execute_kill(dcb->session(), process_id, KT_CONNECTION);
        mxs_mysql_send_ok(dcb, 1, 0, NULL);
        rval = SpecialCmdRes::END;
    }
    else if (m_command == MXS_COM_INIT_DB)
    {
        char* start = (char*)GWBUF_DATA(read_buffer);
        char* end = start + GWBUF_LENGTH(read_buffer);
        start += MYSQL_HEADER_LEN + 1;
        m_session_data->db.assign(start, end);
        m_session->start_database_change(m_session_data->db);
    }
    else if (cmd == MXS_COM_QUERY)
    {
        auto packet_len = gwbuf_length(read_buffer);

        if (is_use_database(read_buffer, packet_len))
        {
            handle_use_database(read_buffer);
        }
        else if (is_kill_query(read_buffer, packet_len))
        {
            rval = handle_query_kill(dcb, read_buffer, packet_len);
        }
    }

    return rval;
}

/**
 * Detect if buffer includes partial mysql packet or multiple packets.
 * Store partial packet to dcb_readqueue. Send complete packets one by one
 * to router.
 *
 * It is assumed readbuf includes at least one complete packet.
 * Return 1 in success. If the last packet is incomplete return success but
 * leave incomplete packet to readbuf.
 *
 * @param capabilities  The capabilities of the service.
 * @param p_readbuf     Pointer to the address of GWBUF including the query
 *
 * @return 1 if succeed,
 */
int MariaDBClientConnection::route_by_statement(uint64_t capabilities, GWBUF** p_readbuf)
{
    int rc = 1;
    auto session = m_session;

    while (GWBUF* packetbuf = modutil_get_next_MySQL_packet(p_readbuf))
    {
        // TODO: Do this only when RCAP_TYPE_CONTIGUOUS_INPUT is requested
        packetbuf = gwbuf_make_contiguous(packetbuf);
        session_retain_statement(session, packetbuf);

        // Track the command being executed
        track_current_command(packetbuf);

        if (char* message = handle_variables(session, &packetbuf))
        {
            rc = write(modutil_create_mysql_err_msg(1, 0, 1193, "HY000", message));
            MXS_FREE(message);
            continue;
        }

        // Must be done whether or not there were any changes, as the query classifier
        // is thread and not session specific.
        qc_set_sql_mode(m_sql_mode);

        if (process_special_commands(m_dcb, packetbuf, m_command) == SpecialCmdRes::END)
        {
            gwbuf_free(packetbuf);
            continue;
        }

        if (rcap_type_required(capabilities, RCAP_TYPE_TRANSACTION_TRACKING)
            && !session->service->config().session_track_trx_state
            && !session_is_load_active(session))
        {
            track_transaction_state(session, packetbuf);
        }

        bool changed_user = false;

        if (!handle_change_user(&changed_user, &packetbuf))
        {
            MXS_ERROR("User reauthentication failed for %s", session->user_and_host().c_str());
            gwbuf_free(packetbuf);
            rc = 0;
            break;
        }

        if (packetbuf)
        {
            /** Route query */
            rc = m_downstream->routeQuery(packetbuf);
        }

        m_changing_user = changed_user;

        if (rc != 1)
        {
            break;
        }
    }

    return rc;
}

/**
 * @brief Client read event, process data, client already authenticated
 *
 * First do some checks and get the router capabilities.  If the router
 * wants to process each individual statement, then the data must be split
 * into individual SQL statements. Any data that is left over is held in the
 * DCB read queue.
 *
 * Finally, the general client data processing function is called.
 *
 * @return True if session should continue, false if client connection should be closed
 */
bool MariaDBClientConnection::perform_normal_read()
{
    auto session_state_value = m_session->state();
    if (session_state_value != MXS_SESSION::State::STARTED)
    {
        if (session_state_value != MXS_SESSION::State::STOPPING)
        {
            MXS_ERROR("Session received a query in incorrect state: %s",
                      session_state_to_string(session_state_value));
        }
        return false;
    }

    mxs::Buffer buffer;
    if (!read_protocol_packet(&buffer))
    {
        return false;
    }
    else if (buffer.empty())
    {
        // Didn't get a complete packet, wait for more data.
        return true;
    }

    // The query classifier classifies according to the service's server that has the smallest version number
    qc_set_server_version(m_version);

    /**
     * Feed each statement completely and separately to router.
     */
    auto read_buffer = buffer.release();
    auto capabilities = service_get_capabilities(m_session->service);
    int ret = route_by_statement(capabilities, &read_buffer) ? 0 : 1;
    mxb_assert(read_buffer == nullptr);     // Router should have consumed the packet.

    bool rval = true;
    if (ret != 0)
    {
        /** Routing failed, close the client connection */
        m_session->close_reason = SESSION_CLOSE_ROUTING_FAILED;
        rval = false;
        MXS_ERROR("Routing the query failed. Session will be closed.");
    }
    else if (m_command == MXS_COM_QUIT)
    {
        /** Close router session which causes closing of backends */
        mxb_assert_message(session_valid_for_pool(m_session), "Session should qualify for pooling");
        rval = false;
    }

    return rval;
}

/**
 * MXS_PROTOCOL_API implementation.
 */

void MariaDBClientConnection::ready_for_reading(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);     // The protocol should only handle its own events.
    bool close = false;

    switch (m_state)
    {
    case State::AUTHENTICATING:
        /**
         * After a listener has accepted a new connection, a standard MySQL handshake is
         * sent to the client. The first time this function is called from the poll loop,
         * the client reply to the handshake should be available.
         */
        if (!perform_authentication())
        {
            close = true;
        }
        break;

    case State::READY:
        // Client connection is authenticated.
        if (!perform_normal_read())
        {
            close = true;
        }
        break;

    case State::FAILED:
        close = true;
        break;

    default:
        MXS_ERROR("In mysql_client.c unexpected protocol authentication state");
        close = true;
        break;
    }

    if (close)
    {
        DCB::close(m_dcb);
    }
}

int32_t MariaDBClientConnection::write(GWBUF* queue)
{
    return m_dcb->writeq_append(queue);
}

void MariaDBClientConnection::write_ready(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);
    mxb_assert(m_dcb->state() != DCB::State::DISCONNECTED);
    if ((m_dcb->state() != DCB::State::DISCONNECTED) && (m_state == State::READY))
    {
        m_dcb->writeq_drain();
    }
}

void MariaDBClientConnection::error(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);
    mxb_assert(m_session->state() != MXS_SESSION::State::STOPPING);
    DCB::close(m_dcb);
}

void MariaDBClientConnection::hangup(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);

    MXS_SESSION* session = m_session;
    if (session && !session_valid_for_pool(session))
    {
        if (session_get_dump_statements() == SESSION_DUMP_STATEMENTS_ON_ERROR)
        {
            session_dump_statements(session);
        }

        if (session_get_session_trace())
        {
            session_dump_log(session);
        }

        // The client did not send a COM_QUIT packet
        std::string errmsg {"Connection killed by MaxScale"};
        std::string extra {session_get_close_reason(m_session)};

        if (!extra.empty())
        {
            errmsg += ": " + extra;
        }

        int seqno = 1;

        if (m_session_data->changing_user)
        {
            // In case a COM_CHANGE_USER is in progress, we need to send the error with the seqno 3
            seqno = 3;
        }

        modutil_send_mysql_err_packet(m_dcb, seqno, 0, 1927, "08S01", errmsg.c_str());
    }

    // We simply close the DCB, this will propagate the closure to any
    // backend descriptors and perform the session cleanup.
    DCB::close(m_dcb);
}

bool MariaDBClientConnection::init_connection()
{
    send_mysql_client_handshake();
    return true;
}

void MariaDBClientConnection::finish_connection()
{
}

int32_t MariaDBClientConnection::connlimit(int limit)
{
    return mysql_send_standard_error(m_dcb, 0, 1040, "Too many connections");
}

MariaDBClientConnection::MariaDBClientConnection(MXS_SESSION* session, mxs::Component* component)
    : m_downstream(component)
    , m_session(session)
    , m_session_data(static_cast<MYSQL_session*>(session->protocol_data()))
    , m_version(service_get_version(session->service, SERVICE_VERSION_MIN))
{
}

/**
 * mysql_send_auth_error
 *
 * Send a MySQL protocol ERR message, for gateway authentication error to the dcb
 *
 * @param dcb descriptor Control Block for the connection to which the OK is sent
 * @param packet_number
 * @param mysql_message
 * @return packet length
 *
 */
int MariaDBClientConnection::mysql_send_auth_error(DCB* dcb, int packet_number, const char* mysql_message)
{
    uint8_t* outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t* mysql_payload = NULL;
    uint8_t field_count = 0;
    uint8_t mysql_err[2];
    uint8_t mysql_statemsg[6];
    const char* mysql_error_msg = NULL;
    const char* mysql_state = NULL;

    GWBUF* buf;

    if (dcb->state() != DCB::State::POLLING)
    {
        MXS_DEBUG("dcb %p is in a state %s, and it is not in epoll set anymore. Skip error sending.",
                  dcb, mxs::to_string(dcb->state()));
        return 0;
    }
    mysql_error_msg = "Access denied!";
    mysql_state = "28000";

    field_count = 0xff;
    gw_mysql_set_byte2(mysql_err,    /*mysql_errno */ 1045);
    mysql_statemsg[0] = '#';
    memcpy(mysql_statemsg + 1, mysql_state, 5);

    if (mysql_message != NULL)
    {
        mysql_error_msg = mysql_message;
    }

    mysql_payload_size =
        sizeof(field_count) + sizeof(mysql_err) + sizeof(mysql_statemsg) + strlen(mysql_error_msg);

    // allocate memory for packet header + payload
    if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
    {
        return 0;
    }
    outbuf = GWBUF_DATA(buf);

    // write packet header with packet number
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
    mysql_packet_header[3] = packet_number;

    // write header
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    mysql_payload = outbuf + sizeof(mysql_packet_header);

    // write field
    memcpy(mysql_payload, &field_count, sizeof(field_count));
    mysql_payload = mysql_payload + sizeof(field_count);

    // write errno
    memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
    mysql_payload = mysql_payload + sizeof(mysql_err);

    // write sqlstate
    memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
    mysql_payload = mysql_payload + sizeof(mysql_statemsg);

    // write err messg
    memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

    // writing data in the Client buffer queue
    dcb->protocol_write(buf);

    return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * Create a message error string to send via MySQL ERR packet.
 *
 * @param       username        The MySQL user
 * @param       hostaddr        The client IP
 * @param       password        If client provided a password
 * @param       db              The default database the client requested
 * @param       errcode         Authentication error code
 *
 * @return      Pointer to the allocated string or NULL on failure
 */
char* MariaDBClientConnection::create_auth_fail_str(const char* username,
                                                    const char* hostaddr,
                                                    bool password,
                                                    const char* db,
                                                    AuthRes errcode)
{
    char* errstr;
    const char* ferrstr;
    int db_len;

    if (db != NULL)
    {
        db_len = strlen(db);
    }
    else
    {
        db_len = 0;
    }

    if (db_len > 0)
    {
        ferrstr = "Access denied for user '%s'@'%s' (using password: %s) to database '%s'";
    }
    else
    {
        ferrstr = "Access denied for user '%s'@'%s' (using password: %s)";
    }
    errstr = (char*)MXS_MALLOC(strlen(username) + strlen(ferrstr)
                               + strlen(hostaddr) + strlen("YES") - 6
                               + db_len + ((db_len > 0) ? (strlen(" to database ") + 2) : 0) + 1);

    if (errstr == NULL)
    {
        goto retblock;
    }

    if (db_len > 0)
    {
        sprintf(errstr, ferrstr, username, hostaddr, password ? "YES" : "NO", db);
    }
    else
    {
        sprintf(errstr, ferrstr, username, hostaddr, password ? "YES" : "NO");
    }

retblock:
    return errstr;
}

/**
 * @brief Send a standard MariaDB error message, emulating real server
 *
 * Supports the sending to a client of a standard database error, for
 * circumstances where the error is generated within MaxScale but should
 * appear like a backend server error. First introduced to support connection
 * throttling, to send "Too many connections" error.
 *
 * @param dcb           The client DCB to which error is to be sent
 * @param packet_number Packet number for header
 * @param error_number  Standard error number as for MariaDB
 * @param error_message Text message to be included
 * @return 0 on failure, 1 on success
 */
int MariaDBClientConnection::mysql_send_standard_error(DCB* dcb, int packet_number, int error_number,
                                                       const char* error_message)
{
    GWBUF* buf = mysql_create_standard_error(packet_number, error_number, error_message);
    return buf ? dcb->protocol_write(buf) : 0;
}

/**
 * @brief Create a standard MariaDB error message, emulating real server
 *
 * Supports the sending to a client of a standard database error, for
 * circumstances where the error is generated within MaxScale but should
 * appear like a backend server error. First introduced to support connection
 * throttling, to send "Too many connections" error.
 *
 * @param packet_number Packet number for header
 * @param error_number  Standard error number as for MariaDB
 * @param error_message Text message to be included
 * @return GWBUF        A buffer containing the error message, ready to send
 */
GWBUF* MariaDBClientConnection::mysql_create_standard_error(int packet_number, int error_number,
                                                            const char* error_message)
{
    uint8_t* outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t mysql_error_number[2];
    uint8_t* mysql_handshake_payload = NULL;
    GWBUF* buf;

    mysql_payload_size = 1 + sizeof(mysql_error_number) + strlen(error_message);

    // allocate memory for packet header + payload
    if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
    {
        return NULL;
    }
    outbuf = GWBUF_DATA(buf);

    // write packet header with mysql_payload_size
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);

    // write packet number, now is 0
    mysql_packet_header[3] = packet_number;
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    // current buffer pointer
    mysql_handshake_payload = outbuf + sizeof(mysql_packet_header);

    // write 0xff which is the error indicator
    *mysql_handshake_payload = 0xff;
    mysql_handshake_payload++;

    // write error number
    gw_mysql_set_byte2(mysql_handshake_payload, error_number);
    mysql_handshake_payload += 2;

    // write error message
    memcpy(mysql_handshake_payload, error_message, strlen(error_message));

    return buf;
}

/**
 * Sends an AuthSwitchRequest packet with the default auth plugin to the client.
 */
bool MariaDBClientConnection::send_auth_switch_request_packet()
{
    const char plugin[] = DEFAULT_MYSQL_AUTH_PLUGIN;
    uint32_t len = 1 + sizeof(plugin) + GW_MYSQL_SCRAMBLE_SIZE;
    GWBUF* buffer = gwbuf_alloc(MYSQL_HEADER_LEN + len);

    uint8_t* data = GWBUF_DATA(buffer);
    gw_mysql_set_byte3(data, len);
    data[3] = 1;    // First response to the COM_CHANGE_USER
    data[MYSQL_HEADER_LEN] = MYSQL_REPLY_AUTHSWITCHREQUEST;
    memcpy(data + MYSQL_HEADER_LEN + 1, plugin, sizeof(plugin));
    memcpy(data + MYSQL_HEADER_LEN + 1 + sizeof(plugin), m_session_data->scramble, MYSQL_SCRAMBLE_LEN);

    return m_dcb->writeq_append(buffer) != 0;
}

void MariaDBClientConnection::execute_kill(MXS_SESSION* issuer, std::shared_ptr<KillInfo> info)
{
    MXS_SESSION* ref = session_get_ref(issuer);
    auto origin = mxs::RoutingWorker::get_current();

    auto func = [info, ref, origin]() {
            // First, gather the list of servers where the KILL should be sent
            mxs::RoutingWorker::execute_concurrently(
                [info, ref]() {
                    dcb_foreach_local(info->cb, info.get());
                });

            // Then move execution back to the original worker to keep all connections on the same thread
            origin->call(
                [info, ref]() {
                    for (const auto& a : info->targets)
                    {
                        if (LocalClient* client = LocalClient::create(info->session, a.first))
                        {
                            client->connect();
                            // TODO: There can be multiple connections to the same server
                            client->queue_query(modutil_create_query(a.second.c_str()));

                            // The LocalClient needs to delete itself once the queries are done
                            client->self_destruct();
                        }
                    }

                    session_put_ref(ref);
                }, mxs::RoutingWorker::EXECUTE_AUTO);
        };

    std::thread(func).detach();
}

void MariaDBClientConnection::mxs_mysql_execute_kill(MXS_SESSION* issuer, uint64_t target_id,
                                                     kill_type_t type)
{
    mxs_mysql_execute_kill_all_others(issuer, target_id, 0, type);
}

/**
 * Send KILL to all but the keep_protocol_thread_id. If keep_protocol_thread_id==0, kill all.
 * TODO: The naming: issuer, target_id, protocol_thread_id is not very descriptive,
 *       and really goes to the heart of explaining what the session_id/thread_id means in terms
 *       of a service/server pipeline and the recursiveness of this call.
 */
void MariaDBClientConnection::mxs_mysql_execute_kill_all_others(MXS_SESSION* issuer,
                                                                uint64_t target_id,
                                                                uint64_t keep_protocol_thread_id,
                                                                kill_type_t type)
{
    const char* hard = (type & KT_HARD) ? "HARD " : (type & KT_SOFT) ? "SOFT " : "";
    const char* query = (type & KT_QUERY) ? "QUERY " : "";
    std::stringstream ss;
    ss << "KILL " << hard << query;

    auto info = std::make_shared<ConnKillInfo>(target_id, ss.str(), issuer, keep_protocol_thread_id);
    execute_kill(issuer, info);
}

void MariaDBClientConnection::mxs_mysql_execute_kill_user(MXS_SESSION* issuer,
                                                          const char* user,
                                                          kill_type_t type)
{
    const char* hard = (type & KT_HARD) ? "HARD " : (type & KT_SOFT) ? "SOFT " : "";
    const char* query = (type & KT_QUERY) ? "QUERY " : "";
    std::stringstream ss;
    ss << "KILL " << hard << query << "USER " << user;

    auto info = std::make_shared<UserKillInfo>(user, ss.str(), issuer);
    execute_kill(issuer, info);
}

std::string MariaDBClientConnection::current_db() const
{
    return m_session_data->db;
}

void MariaDBClientConnection::track_current_command(GWBUF* buffer)
{
    mxb_assert(gwbuf_is_contiguous(buffer));
    uint8_t* data = GWBUF_DATA(buffer);

    if (m_changing_user)
    {
        // User reauthentication in progress, ignore the contents.
        return;
    }

    if (!m_large_query)
    {
        m_command = MYSQL_GET_COMMAND(data);

        if (mxs_mysql_command_will_respond(m_command))
        {
            session_retain_statement(m_session, buffer);
        }
    }

    /**
     * If the buffer contains a large query, we have to skip the command
     * byte extraction for the next packet. This way current_command always
     * contains the latest command executed on this backend.
     */
    m_large_query = MYSQL_GET_PAYLOAD_LEN(data) == MYSQL_PACKET_LENGTH_MAX;
}

const MariaDBUserCache* MariaDBClientConnection::user_account_cache()
{
    auto users = m_session->service->user_account_cache();
    return static_cast<const MariaDBUserCache*>(users);
}

mariadb::UserSearchSettings MariaDBClientConnection::user_search_settings() const
{
    mariadb::UserSearchSettings rval(*m_session_data->user_search_settings);
    auto& service_settings = m_session->service->config();
    rval.allow_root_user = service_settings.enable_root;
    rval.localhost_match_wildcard_host = service_settings.localhost_match_wildcard_host;
    return rval;
}

bool MariaDBClientConnection::parse_ssl_request_packet(GWBUF* buffer)
{
    size_t len = gwbuf_length(buffer);
    // The packet length should be exactly header + 32 = 36 bytes.
    bool rval = false;
    if (len == MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        uint8_t data[CLIENT_CAPABILITIES_LEN];
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, CLIENT_CAPABILITIES_LEN, data);
        parse_client_capabilities(data);
        rval = true;
    }
    return rval;
}

bool MariaDBClientConnection::parse_handshake_response_packet(GWBUF* buffer)
{
    size_t buflen = gwbuf_length(buffer);
    bool rval = false;

    /**
     * The packet should contain client capabilities at the beginning. Some other fields are also
     * obligatory, so length should be at least 38 bytes. Likely there is more.
     *
     * Use a maximum limit as well to prevent stack overflow with malicious clients. The limit
     * is just a guess, but it seems the packets from most plugins are < 100 bytes.
     */
    size_t min_expected_len = NORMAL_HS_RESP_MIN_SIZE;
    auto max_expected_len = min_expected_len + MYSQL_USER_MAXLEN + MYSQL_DATABASE_MAXLEN + 1000;
    if ((buflen >= min_expected_len) && buflen <= max_expected_len)
    {
        int datalen = buflen - MYSQL_HEADER_LEN;
        uint8_t data[datalen + 1];
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, datalen, data);
        data[datalen] = '\0';   // Simplifies some later parsing.
        parse_client_capabilities(data);
        rval = parse_client_response(data + CLIENT_CAPABILITIES_LEN, datalen - CLIENT_CAPABILITIES_LEN);
    }
    return rval;
}

/**
 * Parse 32 bytes of client capabilities.
 *
 * @param data Data array. Should be at least 32 bytes.
 */
void MariaDBClientConnection::parse_client_capabilities(const uint8_t* data)
{
    MYSQL_session* ses = m_session_data;
    auto capabilities = ses->client_info.m_client_capabilities;
    // Can assume that client capabilities are in the first 32 bytes and the buffer is large enough.

    /**
     * We OR the capability bits in order to retain the starting bits sent
     * when an SSL connection is opened. Oracle Connector/J 8.0 appears to drop
     * the SSL capability bit mid-authentication which causes MaxScale to think
     * that SSL is not used.
     */
    capabilities |= gw_mysql_get_byte4(data);
    data += 4;

    // Next is max packet size, skip it.
    data += 4;

    ses->client_info.m_charset = *data;
    data += 1;

    // Next, 19 bytes of reserved filler. Skip.
    data += 19;

    /**
     * Next, 4 bytes of extra capabilities. Not always used.
     * MariaDB 10.2 compatible clients don't set the first bit to signal that
     * there are extra capabilities stored in the last 4 bytes of the filler.
     */
    if ((capabilities & GW_MYSQL_CAPABILITIES_CLIENT_MYSQL) == 0)
    {
        ses->client_info.m_extra_capabilities |= gw_mysql_get_byte4(data);
    }

    ses->client_info.m_client_capabilities = capabilities;
}

/**
 * Parse username, database etc from client handshake response. Client capabilities should have
 * already been parsed.
 *
 * @param data Start of data. Buffer should be long enough to contain at least one item.
 * @param data_len Data length
 * @return
 */
bool MariaDBClientConnection::parse_client_response(const uint8_t* data, int data_len)
{
    auto ptr = (const char*)data;
    const auto end = ptr + data_len;

    // A null-terminated username should be first.
    auto userz = (const char*)data;
    auto userlen = strlen(userz);   // Cannot overrun since caller added 0 to end of buffer.
    ptr += userlen + 1;

    bool error = false;
    auto client_caps = m_session_data->client_info.m_client_capabilities;

    auto read_str = [client_caps, end, &ptr, &error](uint32_t required_capability, const char** output) {
            if (client_caps & required_capability)
            {
                if (ptr < end)
                {
                    auto result = ptr;
                    *output = result;
                    ptr += strlen(result) + 1;      // Should be null-terminated.
                }
                else
                {
                    error = true;
                }
            }
        };


    if (ptr < end)
    {
        // Next is authentication response. The length is encoded in different forms depending on
        // capabilities.
        uint64_t len_remaining = end - ptr;
        uint64_t auth_token_len_bytes = 0;  // In how many bytes the auth token length is encoded in.
        uint64_t auth_token_len = 0;        // The actual auth token length.
        const char* auth_token = nullptr;

        if (client_caps & GW_MYSQL_CAPABILITIES_AUTH_LENENC_DATA)
        {
            // Token is a length-encoded string. First is a length-encoded integer, then the token data.
            auth_token_len_bytes = mxq::leint_bytes((const uint8_t*)ptr);
            if (auth_token_len_bytes <= len_remaining)
            {
                auth_token_len = mxq::leint_value((const uint8_t*)ptr);
            }
            else
            {
                error = true;
            }
        }
        else if (client_caps & GW_MYSQL_CAPABILITIES_SECURE_CONNECTION)
        {
            // First token length 1 byte, then token data.
            auth_token_len_bytes = 1;
            auth_token_len = *ptr;
        }
        else
        {
            MXB_ERROR("Client %s@%s attempted to connect with pre-4.1 authentication "
                      "which is not supported.", userz, m_dcb->remote().c_str());
            error = true;   // Filler was non-zero, likely due to unsupported client version.
        }

        if (!error)
        {
            if (auth_token_len_bytes + auth_token_len <= len_remaining)
            {
                ptr += auth_token_len_bytes;
                if (auth_token_len > 0)
                {
                    auth_token = ptr;
                }
                ptr += auth_token_len;
            }
            else
            {
                error = true;
            }
        }

        const char* db = nullptr;
        const char* plugin = nullptr;
        if (!error)
        {
            // The following fields are optional.
            read_str(GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB, &db);
            read_str(GW_MYSQL_CAPABILITIES_PLUGIN_AUTH, &plugin);

            if ((client_caps & GW_MYSQL_CAPABILITIES_CONNECT_ATTRS) && ptr < end)
            {
                auto n_bytes = mxq::leint_bytes((const uint8_t*)ptr);

                if (ptr + n_bytes < end)
                {
                    n_bytes += mxq::leint_value((const uint8_t*)ptr);

                    if (ptr + n_bytes <= end)
                    {
                        // Store the client connection attributes and reuse them for backend connections. The
                        // data is not processed into key-value pairs as it is not used by MaxScale.
                        m_session_data->connect_attrs.resize(n_bytes);
                        memcpy(&m_session_data->connect_attrs[0], ptr, n_bytes);
                    }
                }
            }
        }

        // TODO: read client attributes
        if (!error)
        {
            // Success, save data to session.
            m_session_data->user = userz;
            if (auth_token)
            {
                auto& ses_auth_token = m_session_data->auth_token;
                ses_auth_token.resize(auth_token_len);
                ses_auth_token.assign(auth_token, auth_token + auth_token_len);
            }

            if (db)
            {
                m_session_data->db = db;
                m_session->set_database(db);
            }

            if (plugin)
            {
                m_session_data->plugin = plugin;
            }
        }
    }
    return !error;
}

/**
 * Read a complete MySQL-protocol packet to output buffer. Returns false on read error.
 *
 * @param output Output for read packet. Should be empty before calling.
 * @return True, if reading succeeded. Also returns true if the entire packet was not yet available and
 * the function should be called again later.
 */
bool MariaDBClientConnection::read_protocol_packet(mxs::Buffer* output)
{
    // TODO: add optimization where the dcb readq is checked first, as it may contain a complete protocol
    // packet.
    GWBUF* read_buffer = nullptr;
    int buffer_len = m_dcb->read(&read_buffer, MAX_PACKET_SIZE);
    if (buffer_len < 0)
    {
        return false;
    }

    if (buffer_len >= MYSQL_HEADER_LEN)
    {
        // Got enough that the entire packet may be available.
        int prot_packet_len = parse_packet_length(read_buffer);

        // Protocol packet length read. Either received more than the packet, the exact packet or
        // a partial packet.
        if (prot_packet_len < buffer_len)
        {
            // Got more than needed, save extra to DCB and trigger a read.
            auto first_packet = gwbuf_split(&read_buffer, prot_packet_len);
            output->reset(first_packet);
            m_dcb->readq_prepend(read_buffer);
            m_dcb->trigger_read_event();
        }
        else if (prot_packet_len == buffer_len)
        {
            // Read exact packet. Return it.
            output->reset(read_buffer);
            if (buffer_len == MAX_PACKET_SIZE && m_dcb->socket_bytes_readable() > 0)
            {
                // Read a maximally long packet when socket has even more. Route this packet,
                // then read again.
                m_dcb->trigger_read_event();
            }
        }
        else
        {
            // Could not read enough, try again later. Save results to dcb.
            m_dcb->readq_prepend(read_buffer);
        }
    }
    else if (buffer_len > 0)
    {
        // Too little data. Save and wait for more.
        m_dcb->readq_prepend(read_buffer);
    }
    else
    {
        // No data was read even though event handler was called. This may happen due to manually triggered
        // reads (e.g. during SSL-init).
    }
    return true;
}

bool MariaDBClientConnection::require_ssl() const
{
    return m_session->listener_data()->m_ssl.valid();
}

bool MariaDBClientConnection::read_first_client_packet(mxs::Buffer* output)
{
    /**
     * Client may send two different kinds of handshakes with different lengths: SSLRequest 36 bytes,
     * or the normal reply >= 38 bytes. If sending the SSLRequest, the client may have added
     * SSL-specific data after the protocol packet. This data should not be read out of the socket,
     * as SSL_accept() will expect to read it.
     *
     * To maintain compatibility with both, read in two steps. This adds one extra read during
     * authentication for non-ssl-connections.
     */
    GWBUF* read_buffer = nullptr;
    int buffer_len = m_dcb->read(&read_buffer, SSL_REQUEST_PACKET_SIZE);
    if (buffer_len < 0)
    {
        return false;
    }

    int prot_packet_len = 0;
    if (buffer_len >= MYSQL_HEADER_LEN)
    {
        prot_packet_len = parse_packet_length(read_buffer);
    }
    else
    {
        // Didn't read enough, try again.
        m_dcb->readq_prepend(read_buffer);
        return true;
    }

    // Got the protocol packet length.
    bool rval = true;
    if (prot_packet_len == SSL_REQUEST_PACKET_SIZE)
    {
        // SSLRequest packet. Most likely the entire packet was already read out. If not, try again later.
        if (buffer_len < prot_packet_len)
        {
            m_dcb->readq_prepend(read_buffer);
            read_buffer = nullptr;
        }
    }
    else if (prot_packet_len >= NORMAL_HS_RESP_MIN_SIZE)
    {
        // Normal response. Need to read again. Likely the entire packet is available at the socket.
        int ret = m_dcb->read(&read_buffer, prot_packet_len);
        buffer_len = gwbuf_length(read_buffer);
        if (ret < 0)
        {
            rval = false;
        }
        else if (buffer_len < prot_packet_len)
        {
            // Still didn't get the full response.
            m_dcb->readq_prepend(read_buffer);
            read_buffer = nullptr;
        }
    }
    else
    {
        // Unexpected packet size.
        rval = false;
    }

    if (rval)
    {
        output->reset(read_buffer);
    }
    else
    {
        // Free any previously read data.
        gwbuf_free(read_buffer);
    }
    return rval;
}
