/*
 *
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#include <maxscale/event.hh>
#include <maxscale/version.h>

#include "setparser.hh"
#include "sqlmodeparser.hh"
#include "user_data.hh"
#include "packet_parser.hh"

namespace
{
using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using ExcRes = mariadb::ClientAuthenticator::ExchRes;
using UserEntryType = mariadb::UserEntryType;
using std::move;
using std::string;

const char WORD_KILL[] = "KILL";
const int CLIENT_CAPABILITIES_LEN = 32;
const int SSL_REQUEST_PACKET_SIZE = MYSQL_HEADER_LEN + CLIENT_CAPABILITIES_LEN;
const int NORMAL_HS_RESP_MIN_SIZE = MYSQL_AUTH_PACKET_BASE_SIZE + 2;
const int MAX_PACKET_SIZE = MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN;

// Default version string sent to clients
const string default_version = "5.5.5-10.2.12 " MAXSCALE_VERSION "-maxscale";

string get_version_string(SERVICE* service)
{
    string service_vrs = service->version_string();
    if (service_vrs.empty())
    {
        auto& custom_suffix = service->custom_version_suffix();
        return custom_suffix.empty() ? default_version : default_version + custom_suffix;
    }

    // Older applications don't understand versions other than 5 and cause strange problems.
    // The MariaDB Server also prepends 5.5.5- to its version strings, and this is not shown by clients.
    if (service_vrs[0] != '5' && service_vrs[0] != '8')
    {
        const char prefix[] = "5.5.5-";
        service_vrs = prefix + service_vrs;
    }
    return service_vrs;
}

enum class CapTypes
{
    XPAND,      // XPand, doesn't include SESSION_TRACK as it doesn't support it
    NORMAL,     // The normal capabilities but without the extra MariaDB-only bits
    MARIADB,    // All capabilities
};

CapTypes get_supported_cap_types(SERVICE* service)
{
    for (SERVER* s : service->reachable_servers())
    {
        if (s->info().type() == SERVER::VersionInfo::Type::XPAND)
        {
            // At least one node is XPand and since it's the most restrictive, we can return early.
            return CapTypes::XPAND;
        }
        else if (s->info().version_num().total >= 100200)
        {
            // TODO: This is "bug compatible" with older releases where the MariaDB extra capabilities are
            // sent if at least one node is version 10.2 or higher. This should behave according to the lowest
            // version reachable from this service to make sure all nodes behave in a roughly similar manner.
            return CapTypes::MARIADB;
        }
    }

    return CapTypes::NORMAL;
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
        prot_packet_len = mariadb::get_byte3(header) + MYSQL_HEADER_LEN;
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

    /* gen_random_bytes() generates random bytes (0-255). This is ok as scramble for most clients
     * (e.g. mariadb) but not for mysql-connector-java. To be on the safe side, ensure every byte
     * is a non-whitespace character. To do the rescaling of values without noticeable bias, generate
     * double the required bytes.
     */
    uint8_t server_scramble[GW_MYSQL_SCRAMBLE_SIZE];
    uint8_t random_bytes[2 * sizeof(server_scramble)];
    mxb::Worker::gen_random_bytes(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(server_scramble); i++)
    {
        auto ptr = &random_bytes[2 * i];
        auto val16 = *(reinterpret_cast<uint16_t*>(ptr));
        server_scramble[i] = '!' + (val16 % (('~' + 1) - '!'));
    }

    auto cap_types = get_supported_cap_types(service);
    bool is_maria = cap_types == CapTypes::MARIADB;

    // copy back to the caller
    memcpy(m_session_data->scramble, server_scramble, GW_MYSQL_SCRAMBLE_SIZE);

    if (is_maria)
    {
        /**
         * The new 10.2 capability flags are stored in the last 4 bytes of the
         * 10 byte filler block.
         */
        uint32_t new_flags = MXS_EXTRA_CAPABILITIES_SERVER;
        memcpy(mysql_filler_ten + 6, &new_flags, sizeof(new_flags));
    }

    // Send the session id as the server thread id. Only the low 32bits are sent in the handshake.
    auto thread_id = m_session->id();
    mariadb::set_byte4(mysql_thread_id_num, (uint32_t)(thread_id));
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
    mariadb::set_byte3(mysql_packet_header, mysql_payload_size);

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

    uint64_t server_capabilities = GW_MYSQL_CAPABILITIES_SERVER;

    if (cap_types == CapTypes::XPAND)
    {
        // XPand doesn't support SESSION_TRACK
        server_capabilities &= ~GW_MYSQL_CAPABILITIES_SESSION_TRACK;
    }

    // write server capabilities part one
    mysql_server_capabilities_one[0] = (uint8_t)server_capabilities;
    mysql_server_capabilities_one[1] = (uint8_t)(server_capabilities >> 8);

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
    mysql_server_capabilities_two[0] = (uint8_t)(server_capabilities >> 16);
    mysql_server_capabilities_two[1] = (uint8_t)(server_capabilities >> 24);

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

    return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * Start or continue authenticating the client.
 *
 * @return Instruction for upper level state machine
 */
MariaDBClientConnection::StateMachineRes
MariaDBClientConnection::process_authentication(AuthType auth_type)
{
    auto rval = StateMachineRes::IN_PROGRESS;
    bool state_machine_continue = true;
    // The referenced object may be updated during this function.
    const auto& user_entry = m_session_data->user_entry;

    while (state_machine_continue)
    {
        switch (m_auth_state)
        {
        case AuthState::FIND_ENTRY:
            {
                update_user_account_entry();
                if (user_entry.type == UserEntryType::USER_ACCOUNT_OK)
                {
                    m_auth_state = AuthState::START_EXCHANGE;
                }
                else
                {
                    // Something is wrong with the entry. Authentication will likely fail.
                    if (user_account_cache()->can_update_immediately())
                    {
                        // User data may be outdated, send update message through the service.
                        // The current session will stall until userdata has been updated.
                        m_session->service->request_user_account_update();
                        m_session->service->mark_for_wakeup(this);
                        m_auth_state = AuthState::TRY_AGAIN;
                        state_machine_continue = false;
                    }
                    else
                    {
                        MXS_WARNING("User accounts have been recently updated, cannot update again for %s.",
                                    m_session->user_and_host().c_str());
                        // If plugin exists, start exchange. Authentication will surely fail.
                        m_auth_state = (user_entry.type == UserEntryType::PLUGIN_IS_NOT_LOADED) ?
                            AuthState::NO_PLUGIN : AuthState::START_EXCHANGE;
                    }
                }
            }
            break;

        case AuthState::TRY_AGAIN:
            {
                // Waiting for user account update.
                if (m_user_update_wakeup)
                {
                    // Only recheck user if the user account data has actually changed since the previous
                    // attempt.
                    if (user_account_cache()->version() > m_previous_userdb_version)
                    {
                        update_user_account_entry();
                    }

                    if (user_entry.type == UserEntryType::USER_ACCOUNT_OK)
                    {
                        MXB_DEBUG("Found user account entry for %s after updating user account data.",
                                  m_session->user_and_host().c_str());
                    }
                    m_auth_state = (user_entry.type == UserEntryType::PLUGIN_IS_NOT_LOADED) ?
                        AuthState::NO_PLUGIN : AuthState::START_EXCHANGE;
                }
                else
                {
                    // Should not get client data (or read events) before users have actually been updated.
                    // This can happen if client hangs up while MaxScale is waiting for the update.
                    MXB_ERROR("Client %s sent data when waiting for user account update. Closing session.",
                              m_session->user_and_host().c_str());
                    send_misc_error("Unexpected client event");
                    // Unmark because auth state is modified.
                    m_session->service->unmark_for_wakeup(this);
                    m_auth_state = AuthState::FAIL;
                }
            }
            break;

        case AuthState::NO_PLUGIN:
            send_authentication_error(AuthErrorType::NO_PLUGIN);
            m_auth_state = AuthState::FAIL;
            break;

        case AuthState::START_EXCHANGE:
        case AuthState::CONTINUE_EXCHANGE:
            state_machine_continue = perform_auth_exchange();
            break;

        case AuthState::CHECK_TOKEN:
            perform_check_token(auth_type);
            break;

        case AuthState::START_SESSION:
            // Authentication success, initialize session.
            if (session_start(m_session))
            {
                mxb_assert(m_session->state() != MXS_SESSION::State::CREATED);
                m_auth_state = AuthState::COMPLETE;
            }
            else
            {
                // Send internal error, as in this case the client has done nothing wrong.
                send_mysql_err_packet(m_session_data->next_sequence, 0, 1815, "HY000",
                                      "Internal error: Session creation failed");
                MXB_ERROR("Failed to create session for %s.", m_session->user_and_host().c_str());
                m_auth_state = AuthState::FAIL;
            }
            break;

        case AuthState::CHANGE_USER_OK:
            {
                // Reauthentication to MaxScale succeeded, but the query still needs to be successfully
                // routed.
                rval = complete_change_user() ? StateMachineRes::DONE : StateMachineRes::ERROR;
                state_machine_continue = false;
                break;
            }

        case AuthState::COMPLETE:
            m_sql_mode = m_session->listener_data()->m_default_sql_mode;
            write_ok_packet(m_session_data->next_sequence);
            if (m_dcb->readq())
            {
                // The user has already sent more data, process it
                m_dcb->trigger_read_event();
            }
            state_machine_continue = false;
            rval = StateMachineRes::DONE;
            break;

        case AuthState::FAIL:
            // An error message should have already been sent.
            state_machine_continue = false;
            if (auth_type == AuthType::NORMAL_AUTH)
            {
                rval = StateMachineRes::ERROR;
            }
            else
            {
                // com_change_user failed, but the session may yet continue.
                cancel_change_user();
                rval = StateMachineRes::DONE;
            }

            break;
        }
    }
    return rval;
}

void MariaDBClientConnection::update_user_account_entry()
{
    const auto mses = m_session_data;
    auto users = user_account_cache();
    auto search_res = users->find_user(mses->user, mses->remote, mses->db, mses->user_search_settings);
    m_previous_userdb_version = users->version();   // Can use this to skip user entry check after update.

    mariadb::AuthenticatorModule* selected_module = nullptr;
    auto& auth_modules = m_session->listener_data()->m_authenticators;
    for (const auto& auth_module : auth_modules)
    {
        auto protocol_auth = static_cast<mariadb::AuthenticatorModule*>(auth_module.get());
        if (protocol_auth->supported_plugins().count(search_res.entry.plugin))
        {
            // Found correct authenticator for the user entry.
            selected_module = protocol_auth;
            break;
        }
    }

    if (selected_module)
    {
        // Correct plugin is loaded, generate session-specific data.
        mses->m_current_authenticator = selected_module;
        // If changing user, this overrides the old client authenticator.
        m_authenticator = selected_module->create_client_authenticator();
    }
    else
    {
        // Authentication cannot continue in this case. Should be rare, though.
        search_res.type = UserEntryType::PLUGIN_IS_NOT_LOADED;
        MXB_INFO("User entry '%s@'%s' uses unrecognized authenticator plugin '%s'. "
                 "Cannot authenticate user.",
                 search_res.entry.username.c_str(), search_res.entry.host_pattern.c_str(),
                 search_res.entry.plugin.c_str());
    }
    mses->user_entry = move(search_res);
}

/**
 * Handle relevant variables
 *
 * @param read_buffer  Pointer to a buffer, assumed to contain a statement.
 *                     May be reallocated if not contiguous.
 *
 * @return NULL if successful, otherwise dynamically allocated error message.
 */
char* MariaDBClientConnection::handle_variables(GWBUF** read_buffer)
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
                    m_session->set_autocommit(false);
                    m_sql_mode = QC_SQL_MODE_ORACLE;
                    break;

                case SqlModeParser::DEFAULT:
                    m_session->set_autocommit(true);
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

                message = session_set_variable_value(m_session,
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

void MariaDBClientConnection::track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf)
{
    mxb_assert(gwbuf_is_contiguous(packetbuf));
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
 * @param read_buffer Input buffer
 * @param packet_len  Length of the protocol packet
 *
 * @return RES_CONTINUE or RES_END
 */
MariaDBClientConnection::SpecialCmdRes
MariaDBClientConnection::handle_query_kill(GWBUF* read_buffer, uint32_t packet_len)
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
                mxs_mysql_execute_kill(thread_id, kt);
            }
            else if (!user.empty())
            {
                execute_kill_user(user.c_str(), kt);
            }

            write_ok_packet(1);
        }
    }

    return rval;
}

void MariaDBClientConnection::handle_use_database(GWBUF* read_buffer)
{
    auto databases = qc_get_database_names(read_buffer);

    if (!databases.empty())
    {
        m_session->start_database_change(databases[0]);
    }
}

/**
 * Some SQL commands/queries need to be detected and handled by the protocol
 * and MaxScale instead of being routed forward as is.
 *
 * @param read_buffer The current read buffer
 * @param cmd         Current command being executed
 *
 * @return see @c spec_com_res_t
 */
MariaDBClientConnection::SpecialCmdRes
MariaDBClientConnection::process_special_commands(GWBUF* read_buffer, uint8_t cmd)
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
        session_qualify_for_pool(m_session);
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
        uint64_t process_id = mariadb::get_byte4(GWBUF_DATA(read_buffer) + MYSQL_HEADER_LEN + 1);
        mxs_mysql_execute_kill(process_id, KT_CONNECTION);
        write_ok_packet(1);
        rval = SpecialCmdRes::END;
    }
    else if (m_command == MXS_COM_INIT_DB)
    {
        char* start = (char*)GWBUF_DATA(read_buffer);
        char* end = start + GWBUF_LENGTH(read_buffer);
        start += MYSQL_HEADER_LEN + 1;
        m_session->start_database_change(std::string(start, end));
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
            rval = handle_query_kill(read_buffer, packet_len);
        }
    }

    return rval;
}

/**
 * Route an SQL protocol packet. If the original client packet is less than 16MB, buffer should
 * contain the complete packet. If the client packet is large (split into multiple protocol packets),
 * only one protocol packet should be routed at a time.
 * TODO: what happens with parsing in this case? Likely it fails.
 *
 * @param buffer     Pointer to the address of GWBUF including the query
 * @return True on success
 */
bool MariaDBClientConnection::route_statement(mxs::Buffer&& buffer)
{
    bool rval = true;
    auto session = m_session;

    GWBUF* packetbuf = buffer.release();
    // TODO: Do this only when RCAP_TYPE_CONTIGUOUS_INPUT is requested
    packetbuf = gwbuf_make_contiguous(packetbuf);

    if (m_routing_state == RoutingState::PACKET_START && mxs_mysql_command_will_respond(m_command))
    {
        session_retain_statement(m_session, packetbuf);
    }

    bool keep_processing = true;
    if (m_command == MXS_COM_QUERY)
    {
        // Track MaxScale-specific sql.
        char* errmsg = handle_variables(&packetbuf);
        if (errmsg)
        {
            gwbuf_free(packetbuf);
            rval = write(modutil_create_mysql_err_msg(1, 0, 1193, "HY000", errmsg)) != 0;
            MXS_FREE(errmsg);
            keep_processing = false;
        }
    }

    if (keep_processing)
    {
        // Must be done whether or not there were any changes, as the query classifier
        // is thread and not session specific.
        qc_set_sql_mode(m_sql_mode);
        // The query classifier classifies according to the service's server that has
        // the smallest version number.
        qc_set_server_version(m_version);

        if (!session_is_load_active(session)
            && process_special_commands(packetbuf, m_command) == SpecialCmdRes::END)
        {
            gwbuf_free(packetbuf);
            packetbuf = nullptr;
            keep_processing = false;
        }

        if (keep_processing)
        {
            auto capabilities = service_get_capabilities(m_session->service);
            if (rcap_type_required(capabilities, RCAP_TYPE_TRANSACTION_TRACKING)
                && !session->service->config()->session_track_trx_state
                && !session_is_load_active(session))
            {
                track_transaction_state(session, packetbuf);
            }

            if (packetbuf)
            {
                rval = m_downstream->routeQuery(packetbuf) != 0;
            }
        }
    }
    return rval;
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
MariaDBClientConnection::StateMachineRes MariaDBClientConnection::process_normal_read()
{
    auto session_state_value = m_session->state();
    if (session_state_value != MXS_SESSION::State::STARTED)
    {
        if (session_state_value != MXS_SESSION::State::STOPPING)
        {
            MXS_ERROR("Session received a query in incorrect state: %s",
                      session_state_to_string(session_state_value));
        }
        return StateMachineRes::ERROR;
    }

    mxs::Buffer buffer;
    if (!read_protocol_packet(m_dcb, &buffer))
    {
        return StateMachineRes::ERROR;
    }
    else if (buffer.empty())
    {
        // Didn't get a complete packet, wait for more data.
        return StateMachineRes::IN_PROGRESS;
    }

    bool routed = false;

    // Backend-protocol tracks LOAD_DATA-state by looking at replies.
    // TODO: add client-side tracking for proper error detection.
    if (session_is_load_active(m_session))
    {
        m_routing_state = RoutingState::LOAD_DATA;
    }

    switch (m_routing_state)
    {
    case RoutingState::PACKET_START:
        if (buffer.length() > MYSQL_HEADER_LEN)
        {
            routed = process_normal_packet(move(buffer));
        }
        else
        {
            // Unexpected, client should not be sending empty (header-only) packets in this case.
            MXS_ERROR("Client %s sent empty packet when a normal packet was expected.",
                      m_session->user_and_host().c_str());
            buffer.reset();
        }
        break;

    case RoutingState::LARGE_PACKET:
        {
            // No command bytes, just continue routing large packet.
            bool is_large = large_query_continues(buffer);
            routed = route_statement(move(buffer));
            if (!is_large)
            {
                // Large packet routing completed.
                m_routing_state = RoutingState::PACKET_START;
            }
        }
        break;

    case RoutingState::LOAD_DATA:
        {
            // Local-infile routing continues until client sends an empty packet. Again, tracked by backend
            // but this time on the downstream side.
            routed = route_statement(move(buffer));
            if (!session_is_load_active(m_session))
            {
                m_routing_state = RoutingState::PACKET_START;
            }
        }
        break;
    }

    auto rval = StateMachineRes::IN_PROGRESS;
    if (!routed)
    {
        /** Routing failed, close the client connection */
        m_session->close_reason = SESSION_CLOSE_ROUTING_FAILED;
        rval = StateMachineRes::ERROR;
        MXS_ERROR("Routing the query failed. Session will be closed.");
    }
    else if (m_command == MXS_COM_QUIT)
    {
        /** Close router session which causes closing of backends */
        mxb_assert_message(session_valid_for_pool(m_session), "Session should qualify for pooling");
        m_state = State::QUIT;
        rval = StateMachineRes::DONE;
    }

    return rval;
}

/**
 * MXS_PROTOCOL_API implementation.
 */

void MariaDBClientConnection::ready_for_reading(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);     // The protocol should only handle its own events.

    bool state_machine_continue = true;
    while (state_machine_continue)
    {
        switch (m_state)
        {
        case State::HANDSHAKING:
            /**
             * After a listener has accepted a new connection, a standard MySQL handshake is
             * sent to the client. The first time this function is called from the poll loop,
             * the client reply to the handshake should be available.
             */
            {
                auto ret = process_handshake();
                switch (ret)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;     // need more data
                    break;

                case StateMachineRes::DONE:
                    m_state = State::AUTHENTICATING;        // continue directly to next state
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::AUTHENTICATING:
        case State::CHANGING_USER:
            {
                auto auth_type = (m_state == State::CHANGING_USER) ? AuthType::CHANGE_USER :
                    AuthType::NORMAL_AUTH;
                auto ret = process_authentication(auth_type);
                switch (ret)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;     // need more data
                    break;

                case StateMachineRes::DONE:
                    m_state = State::READY;
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::READY:
            {
                auto ret = process_normal_read();
                switch (ret)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;
                    break;

                case StateMachineRes::DONE:
                    // In this case, next m_state was written by 'process_normal_read'.
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::QUIT:
        case State::FAILED:
            state_machine_continue = false;
            break;
        }
    }

    if (m_state == State::FAILED || m_state == State::QUIT)
    {
        m_session->kill();
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
    m_session->kill();
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

        MYSQL_session* client_data = static_cast<MYSQL_session*>(session->protocol_data());
        send_mysql_err_packet(client_data->next_sequence, 0, 1927, "08S01", errmsg.c_str());
    }

    // We simply close the session, this will propagate the closure to any
    // backend descriptors and perform the session cleanup.
    m_session->kill();
}

bool MariaDBClientConnection::init_connection()
{
    send_mysql_client_handshake();
    return true;
}

void MariaDBClientConnection::finish_connection()
{
    // If this connection is waiting for userdata, remove the entry.
    if (m_auth_state == AuthState::TRY_AGAIN)
    {
        m_session->service->unmark_for_wakeup(this);
    }
}

int32_t MariaDBClientConnection::connlimit(int limit)
{
    return send_standard_error(0, 1040, "Too many connections");
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
 * @param packet_number
 * @param mysql_message
 * @return packet length
 *
 */
int MariaDBClientConnection::send_auth_error(int packet_number, const char* mysql_message)
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

    mxb_assert(m_dcb->state() == DCB::State::POLLING);
    mysql_error_msg = "Access denied!";
    mysql_state = "28000";

    field_count = 0xff;
    mariadb::set_byte2(mysql_err,    /*mysql_errno */ 1045);
    mysql_statemsg[0] = '#';
    memcpy(mysql_statemsg + 1, mysql_state, 5);

    if (mysql_message != NULL)
    {
        mysql_error_msg = mysql_message;
    }

    mysql_payload_size =
        sizeof(field_count) + sizeof(mysql_err) + sizeof(mysql_statemsg) + strlen(mysql_error_msg);

    // allocate memory for packet header + payload
    GWBUF* buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
    if (!buf)
    {
        return 0;
    }
    outbuf = GWBUF_DATA(buf);

    // write packet header with packet number
    mariadb::set_byte3(mysql_packet_header, mysql_payload_size);
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
    write(buf);

    return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * @brief Send a standard MariaDB error message, emulating real server
 *
 * Supports the sending to a client of a standard database error, for
 * circumstances where the error is generated within MaxScale but should
 * appear like a backend server error. First introduced to support connection
 * throttling, to send "Too many connections" error.
 *
 * @param packet_number Packet number for header
 * @param error_number  Standard error number as for MariaDB
 * @param error_message Text message to be included
 * @return 0 on failure, 1 on success
 */
int MariaDBClientConnection::send_standard_error(int packet_number, int error_number,
                                                 const char* error_message)
{
    GWBUF* buf = create_standard_error(packet_number, error_number, error_message);
    return buf ? write(buf) : 0;
}

/**
 * @brief Create a standard MariaDB error message, emulating real server
 *
 * Supports the sending to a client of a standard database error, for
 * circumstances where the error is generated within MaxScale but should
 * appear like a backend server error. First introduced to support connection
 * throttling, to send "Too many connections" error.
 *
 * @param sequence Packet number for header
 * @param error_number  Standard error number as for MariaDB
 * @param msg Text message to be included
 * @return GWBUF        A buffer containing the error message, ready to send
 */
GWBUF* MariaDBClientConnection::create_standard_error(int packet_number, int error_number,
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
    mariadb::set_byte3(mysql_packet_header, mysql_payload_size);

    // write packet number, now is 0
    mysql_packet_header[3] = packet_number;
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    // current buffer pointer
    mysql_handshake_payload = outbuf + sizeof(mysql_packet_header);

    // write 0xff which is the error indicator
    *mysql_handshake_payload = 0xff;
    mysql_handshake_payload++;

    // write error number
    mariadb::set_byte2(mysql_handshake_payload, error_number);
    mysql_handshake_payload += 2;

    // write error message
    memcpy(mysql_handshake_payload, error_message, strlen(error_message));

    return buf;
}

void MariaDBClientConnection::execute_kill(std::shared_ptr<KillInfo> info)
{
    MXS_SESSION* ref = session_get_ref(m_session);
    auto origin = mxs::RoutingWorker::get_current();

    auto func = [this, info, ref, origin]() {
            // First, gather the list of servers where the KILL should be sent
            mxs::RoutingWorker::execute_concurrently(
                [info, ref]() {
                    dcb_foreach_local(info->cb, info.get());
                });

            // Then move execution back to the original worker to keep all connections on the same thread
            origin->call(
                [this, info, ref]() {
                    for (const auto& a : info->targets)
                    {
                        if (LocalClient* client = LocalClient::create(info->session, a.first))
                        {
                            client->connect();
                            // TODO: There can be multiple connections to the same server. Currently only one
                            // connection per server is killed.
                            client->queue_query(modutil_create_query(a.second.c_str()));
                            client->queue_query(mysql_create_com_quit(NULL, 0));

                            mxb_assert(ref->state() != MXS_SESSION::State::STOPPING);
                            add_local_client(client);
                        }
                    }

                    session_put_ref(ref);
                }, mxs::RoutingWorker::EXECUTE_AUTO);
        };

    std::thread(func).detach();
}

void MariaDBClientConnection::mxs_mysql_execute_kill(uint64_t target_id, kill_type_t type)
{
    execute_kill_all_others(target_id, 0, type);
}

/**
 * Send KILL to all but the keep_protocol_thread_id. If keep_protocol_thread_id==0, kill all.
 * TODO: The naming: issuer, target_id, protocol_thread_id is not very descriptive,
 *       and really goes to the heart of explaining what the session_id/thread_id means in terms
 *       of a service/server pipeline and the recursiveness of this call.
 */
void MariaDBClientConnection::execute_kill_all_others(uint64_t target_id,
                                                      uint64_t keep_protocol_thread_id,
                                                      kill_type_t type)
{
    const char* hard = (type & KT_HARD) ? "HARD " : (type & KT_SOFT) ? "SOFT " : "";
    const char* query = (type & KT_QUERY) ? "QUERY " : "";
    std::stringstream ss;
    ss << "KILL " << hard << query;

    auto info = std::make_shared<ConnKillInfo>(target_id, ss.str(), m_session, keep_protocol_thread_id);
    execute_kill(info);
}

void MariaDBClientConnection::execute_kill_user(const char* user, kill_type_t type)
{
    const char* hard = (type & KT_HARD) ? "HARD " : (type & KT_SOFT) ? "SOFT " : "";
    const char* query = (type & KT_QUERY) ? "QUERY " : "";
    std::stringstream ss;
    ss << "KILL " << hard << query << "USER " << user;

    auto info = std::make_shared<UserKillInfo>(user, ss.str(), m_session);
    execute_kill(info);
}

std::string MariaDBClientConnection::current_db() const
{
    return m_session_data->db;
}

void MariaDBClientConnection::track_current_command(const mxs::Buffer& buffer)
{
    mxb_assert(m_routing_state == RoutingState::PACKET_START);
    const uint8_t* data = GWBUF_DATA(buffer.get());
    m_command = MYSQL_GET_COMMAND(data);
}

const MariaDBUserCache* MariaDBClientConnection::user_account_cache()
{
    auto users = m_session->service->user_account_cache();
    return static_cast<const MariaDBUserCache*>(users);
}

bool MariaDBClientConnection::parse_ssl_request_packet(GWBUF* buffer)
{
    size_t len = gwbuf_length(buffer);
    // The packet length should be exactly header + 32 = 36 bytes.
    bool rval = false;
    if (len == MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        packet_parser::ByteVec data;
        data.resize(CLIENT_CAPABILITIES_LEN);
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, CLIENT_CAPABILITIES_LEN, data.data());
        m_session_data->client_info = packet_parser::parse_client_capabilities(data, nullptr);
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
        packet_parser::ByteVec data;
        data.resize(datalen + 1);
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, datalen, data.data());
        data[datalen] = '\0';   // Simplifies some later parsing.

        auto client_info = packet_parser::parse_client_capabilities(data, &m_session_data->client_info);
        auto parse_res = packet_parser::parse_client_response(data, client_info.m_client_capabilities);

        if (parse_res.success)
        {
            // If the buffer is valid, just one 0 should remain. Some (old) connectors may send malformed
            // packets with extra data. Such packets work, but some data may not be parsed properly.
            auto data_size = data.size();
            if (data_size >= 1)
            {
                // Success, save data to session.
                m_session_data->user = parse_res.username;
                m_session->set_user(parse_res.username);
                m_session_data->auth_token = move(parse_res.token_res.auth_token);
                m_session_data->db = parse_res.db;
                m_session->set_database(parse_res.db);
                m_session_data->plugin = move(parse_res.plugin);

                // Discard the attributes if there is any indication of failed parsing, as the contents
                // may be garbled.
                if (parse_res.success && data_size == 1)
                {
                    m_session_data->connect_attrs = move(parse_res.attr_res.attr_data);
                }
                else
                {
                    client_info.m_client_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_ATTRS;
                }
                m_session_data->client_info = client_info;

                rval = true;
            }
        }
        else if (parse_res.token_res.old_protocol)
        {
            MXB_ERROR("Client %s@%s attempted to connect with pre-4.1 authentication "
                      "which is not supported.", parse_res.username.c_str(), m_dcb->remote().c_str());
        }
    }
    return rval;
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

void MariaDBClientConnection::wakeup()
{
    mxb_assert(m_auth_state == AuthState::TRY_AGAIN);
    m_user_update_wakeup = true;
    m_dcb->trigger_read_event();
}

bool MariaDBClientConnection::is_movable() const
{
    mxb_assert(mxs::RoutingWorker::get_current() == m_dcb->owner);
    return m_auth_state != AuthState::TRY_AGAIN;
}

bool MariaDBClientConnection::start_change_user(mxs::Buffer&& buffer)
{
    // Parse the COM_CHANGE_USER-packet. The packet is somewhat similar to a typical handshake response.
    size_t buflen = buffer.length();
    bool rval = false;

    size_t min_expected_len = MYSQL_HEADER_LEN + 5;
    auto max_expected_len = min_expected_len + MYSQL_USER_MAXLEN + MYSQL_DATABASE_MAXLEN + 1000;
    if ((buflen >= min_expected_len) && buflen <= max_expected_len)
    {
        int datalen = buflen - MYSQL_HEADER_LEN;
        packet_parser::ByteVec data;
        data.resize(datalen + 1);
        gwbuf_copy_data(buffer.get(), MYSQL_HEADER_LEN, datalen, data.data());
        data[datalen] = '\0';   // Simplifies some later parsing.

        auto parse_res = packet_parser::parse_change_user_packet(data, m_session_data->client_capabilities());
        if (parse_res.success)
        {
            // Only the last byte should be left.
            if (data.size() == 1)
            {
                m_change_user.client_query = move(buffer);

                // Make a temporary session for the change user. Some of the fields persist for the new
                // user, some need to be overwritten. The client authenticator does not need to be preserved.
                m_change_user.session = std::make_unique<MYSQL_session>(*m_session_data);
                m_change_user.session->user = parse_res.username;
                m_change_user.session->db = parse_res.db;
                m_change_user.session->plugin = parse_res.plugin;
                m_change_user.session->client_info.m_charset = parse_res.charset;
                m_change_user.session->auth_token = parse_res.token_res.auth_token;
                m_change_user.session->connect_attrs = parse_res.attr_res.attr_data;

                // Point the session used by the connection to the temporary session so other authentication-
                // related functions access it. Backend connections will still see the old session data.
                m_session_data = m_change_user.session.get();
                rval = true;
                MXB_INFO("Client %s is attempting a COM_CHANGE_USER to '%s'.",
                         m_session->user_and_host().c_str(), m_change_user.session->user.c_str());
            }
        }
        else if (parse_res.token_res.old_protocol)
        {
            MXB_ERROR("Client %s attempted a COM_CHANGE_USER with pre-4.1 authentication, "
                      "which is not supported.", m_session->user_and_host().c_str());
        }
    }
    return rval;
}

bool MariaDBClientConnection::complete_change_user()
{
    // Finalize results by writing session-level objects and routing the original change-user packet.
    if (m_change_user.session->user_entry.entry.super_priv && mxs::Config::get().log_warn_super_user)
    {
        MXB_WARNING("COM_CHANGE_USER from %s to super user '%s' in service '%s'.",
                    m_session->user_and_host().c_str(), m_change_user.session->user.c_str(),
                    m_session->service->name());
    }
    else
    {
        MXB_INFO("COM_CHANGE_USER from %s to '%s' in service '%s' succeeded.",
                 m_session->user_and_host().c_str(), m_change_user.session->user.c_str(),
                 m_session->service->name());
    }
    m_session_data = static_cast<MYSQL_session*>(m_session->protocol_data());
    // The old session data must be overwritten in-place, as other connections etc may have
    // saved pointers to it.
    *m_session_data = *m_change_user.session;
    m_change_user.session.reset();
    bool rval = route_statement(move(m_change_user.client_query));
    return rval;
}

void MariaDBClientConnection::cancel_change_user()
{
    MXB_INFO("COM_CHANGE_USER from %s to '%s' failed.",
             m_session->user_and_host().c_str(), m_change_user.session->user.c_str());
    // Cancel by restoring old values. An error message should have been sent to the client.
    m_session_data = static_cast<MYSQL_session*>(m_session->protocol_data());
    m_change_user.client_query.reset();
    m_change_user.session = nullptr;
}

MariaDBClientConnection::StateMachineRes MariaDBClientConnection::process_handshake()
{
    mxs::Buffer read_buffer;
    bool read_success = (m_handshake_state == HSState::INIT) ?
        // The first response from client requires special handling.
        read_first_client_packet(&read_buffer) : read_protocol_packet(m_dcb, &read_buffer);

    if (!read_success)
    {
        return StateMachineRes::ERROR;
    }
    else if (read_buffer.empty())
    {
        // Not enough data was available yet.
        return StateMachineRes::IN_PROGRESS;
    }

    auto buffer = read_buffer.get();
    update_sequence(buffer);
    uint8_t next_seq = m_sequence + 1;
    m_session_data->next_sequence = next_seq;

    const char wrong_sequence[] = "Client (%s) sent packet with unexpected sequence number. "
                                  "Expected %i, got %i.";
    const char packets_ooo[] = "Got packets out of order";
    const char sql_errstate[] = "08S01";
    const int er_bad_handshake = 1043;
    const int er_out_of_order = 1156;

    auto rval = StateMachineRes::IN_PROGRESS;   // Returned to upper level SM
    bool state_machine_continue = true;
    while (state_machine_continue)
    {
        switch (m_handshake_state)
        {
        case HSState::INIT:
            m_handshake_state = require_ssl() ? HSState::EXPECT_SSL_REQ : HSState::EXPECT_HS_RESP;
            break;

        case HSState::EXPECT_SSL_REQ:
            {
                // Expecting SSLRequest
                if (m_sequence == 1)
                {
                    if (parse_ssl_request_packet(buffer))
                    {
                        m_handshake_state = HSState::SSL_NEG;
                    }
                    else if (parse_handshake_response_packet(buffer))
                    {
                        send_authentication_error(AuthErrorType::ACCESS_DENIED);
                        m_handshake_state = HSState::FAIL;
                    }
                    else
                    {
                        send_mysql_err_packet(next_seq, 0, er_bad_handshake, sql_errstate,
                                              "Bad SSL handshake");
                        MXB_ERROR("Client (%s) sent an invalid SSLRequest.", m_dcb->remote().c_str());
                        m_handshake_state = HSState::FAIL;
                    }
                }
                else
                {
                    send_mysql_err_packet(next_seq, 0, er_out_of_order, sql_errstate, packets_ooo);
                    MXB_ERROR(wrong_sequence, m_session_data->remote.c_str(), 1, m_sequence);
                    m_handshake_state = HSState::FAIL;
                }
            }
            break;

        case HSState::SSL_NEG:
            {
                // Client should be negotiating ssl.
                auto ssl_status = ssl_authenticate_check_status();
                if (ssl_status == SSLState::COMPLETE)
                {
                    m_handshake_state = HSState::EXPECT_HS_RESP;
                }
                else if (ssl_status == SSLState::INCOMPLETE)
                {
                    // SSL negotiation should complete in the background. Execution returns here once
                    // complete.
                    state_machine_continue = false;
                }
                else
                {
                    send_auth_error(next_seq, "Access without SSL denied");
                    MXB_ERROR("Client (%s) failed SSL negotiation.", m_session_data->remote.c_str());
                    m_handshake_state = HSState::FAIL;
                }
            }
            break;

        case HSState::EXPECT_HS_RESP:
            {
                // Expecting normal Handshake response
                // @see https://mariadb.com/kb/en/library/connection/#client-handshake-response
                int expected_seq = require_ssl() ? 2 : 1;
                if (m_sequence == expected_seq)
                {
                    if (parse_handshake_response_packet(buffer))
                    {
                        m_handshake_state = HSState::COMPLETE;
                    }
                    else
                    {
                        send_mysql_err_packet(next_seq, 0, er_bad_handshake, sql_errstate,
                                              "Bad handshake");
                        MXB_ERROR("Client (%s) sent an invalid HandShakeResponse.",
                                  m_session_data->remote.c_str());
                        m_handshake_state = HSState::FAIL;
                    }
                }
                else
                {
                    send_mysql_err_packet(next_seq, 0, er_out_of_order, sql_errstate, packets_ooo);
                    MXB_ERROR(wrong_sequence, m_session_data->remote.c_str(), expected_seq, m_sequence);
                    m_handshake_state = HSState::FAIL;
                }
            }
            break;

        case HSState::COMPLETE:
            state_machine_continue = false;
            rval = StateMachineRes::DONE;
            break;

        case HSState::FAIL:
            // An error message should have already been sent.
            state_machine_continue = false;
            rval = StateMachineRes::ERROR;
            break;
        }
    }
    return rval;
}

void MariaDBClientConnection::update_sequence(GWBUF* buf)
{
    mxb_assert(gwbuf_length(buf) >= MYSQL_HEADER_LEN);
    gwbuf_copy_data(buf, MYSQL_SEQ_OFFSET, 1, &m_sequence);
}

void MariaDBClientConnection::send_authentication_error(AuthErrorType error, const std::string& auth_mod_msg)
{
    auto ses = m_session_data;
    string mariadb_msg;

    switch (error)
    {
    case AuthErrorType::ACCESS_DENIED:
        mariadb_msg = mxb::string_printf("Access denied for user '%s'@'%s' (using password: %s)",
                                         ses->user.c_str(), ses->remote.c_str(),
                                         ses->auth_token.empty() ? "NO" : "YES");
        send_mysql_err_packet(ses->next_sequence, 0, 1045, "28000", mariadb_msg.c_str());
        break;

    case AuthErrorType::DB_ACCESS_DENIED:
        mariadb_msg = mxb::string_printf("Access denied for user '%s'@'%s' to database '%s'",
                                         ses->user.c_str(), ses->remote.c_str(), ses->db.c_str());
        send_mysql_err_packet(ses->next_sequence, 0, 1044, "42000", mariadb_msg.c_str());
        break;

    case AuthErrorType::BAD_DB:
        mariadb_msg = mxb::string_printf("Unknown database '%s'", ses->db.c_str());
        send_mysql_err_packet(ses->next_sequence, 0, 1049, "42000", mariadb_msg.c_str());
        break;

    case AuthErrorType::NO_PLUGIN:
        mariadb_msg = mxb::string_printf("Plugin '%s' is not loaded", ses->user_entry.entry.plugin.c_str());
        send_mysql_err_packet(ses->next_sequence, 0, 1524, "HY000", mariadb_msg.c_str());
        break;
    }

    // Also log an authentication failure event.
    if (m_session->service->config()->log_auth_warnings)
    {
        string total_msg = mxb::string_printf("Authentication failed for user '%s'@[%s] to service '%s'. "
                                              "Originating listener: '%s'. MariaDB error: '%s'.",
                                              ses->user.c_str(), ses->remote.c_str(),
                                              m_session->service->name(),
                                              m_session->listener_data()->m_listener_name.c_str(),
                                              mariadb_msg.c_str());
        if (!auth_mod_msg.empty())
        {
            total_msg += mxb::string_printf(" Authenticator error: '%s'.", auth_mod_msg.c_str());
        }
        MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE, "%s", total_msg.c_str());
    }
}

void MariaDBClientConnection::send_misc_error(const std::string& msg)
{
    send_mysql_err_packet(m_session_data->next_sequence, 0, 1105, "HY000", msg.c_str());
}

/**
 * Authentication exchange state for authenticator state machine.
 *
 * @return True, if the calling state machine should continue. False, if it should wait for more client data.
 */
bool MariaDBClientConnection::perform_auth_exchange()
{
    mxb_assert(m_auth_state == AuthState::START_EXCHANGE || m_auth_state == AuthState::CONTINUE_EXCHANGE);

    mxs::Buffer read_buffer;
    // Nothing to read on first exchange-call.
    if (m_auth_state == AuthState::CONTINUE_EXCHANGE)
    {
        if (read_protocol_packet(m_dcb, &read_buffer))
        {
            if (read_buffer.empty())
            {
                // Not enough data was available yet.
                return false;
            }
            else
            {
                update_sequence(read_buffer.get());
                // Save next sequence to session. Authenticator may use the value.
                m_session_data->next_sequence = m_sequence + 1;
            }
        }
        else
        {
            // Connection is likely broken, no need to send error message.
            m_auth_state = AuthState::FAIL;
            return true;
        }
    }

    mxs::Buffer auth_output;
    auto auth_val = m_authenticator->exchange(read_buffer.get(), m_session_data, &auth_output);
    if (!auth_output.empty())
    {
        write(auth_output.release());
    }

    bool state_machine_continue = true;
    if (auth_val == ExcRes::READY)
    {
        // Continue to password check.
        m_auth_state = AuthState::CHECK_TOKEN;
    }
    else if (auth_val == ExcRes::INCOMPLETE)
    {
        // Authentication is expecting another packet from client, so jump out.
        if (m_auth_state == AuthState::START_EXCHANGE)
        {
            m_auth_state = AuthState::CONTINUE_EXCHANGE;
        }
        state_machine_continue = false;
    }
    else
    {
        // Exchange failed. Usually a communication or memory error.
        auto msg = mxb::string_printf("Authentication plugin '%s' failed",
                                      m_session_data->m_current_authenticator->name().c_str());
        send_misc_error(msg);
        m_auth_state = AuthState::FAIL;
    }
    return state_machine_continue;
}

void MariaDBClientConnection::perform_check_token(AuthType auth_type)
{
    // If the user entry didn't exist in the first place, don't check token and just fail.
    // TODO: server likely checks some random token to spend time, could add it later.
    const auto& user_entry = m_session_data->user_entry;
    const auto entrytype = user_entry.type;

    if (entrytype == UserEntryType::USER_NOT_FOUND)
    {
        send_authentication_error(AuthErrorType::ACCESS_DENIED);
        m_auth_state = AuthState::FAIL;
    }
    else
    {
        AuthRes auth_val;
        if (m_session_data->user_search_settings.listener.check_password)
        {
            auth_val = m_authenticator->authenticate(&user_entry.entry, m_session_data);
        }
        else
        {
            auth_val.status = AuthRes::Status::SUCCESS;
        }

        if (auth_val.status == AuthRes::Status::SUCCESS)
        {
            if (entrytype == UserEntryType::USER_ACCOUNT_OK)
            {
                // Authentication succeeded. If the user has super privileges, print a warning. The change-
                // user equivalent is printed elsewhere.
                if (auth_type == AuthType::NORMAL_AUTH)
                {
                    m_auth_state = AuthState::START_SESSION;
                    if (user_entry.entry.super_priv && mxs::Config::get().log_warn_super_user)
                    {
                        MXB_WARNING("Super user %s logged in to service '%s'.",
                                    m_session_data->user_and_host().c_str(), m_session->service->name());
                    }
                }
                else
                {
                    m_auth_state = AuthState::CHANGE_USER_OK;
                }
            }
            else
            {
                // Translate the original user account search error type to an error message type.
                auto error = AuthErrorType::ACCESS_DENIED;
                switch (entrytype)
                {
                case UserEntryType::DB_ACCESS_DENIED:
                    error = AuthErrorType::DB_ACCESS_DENIED;
                    break;

                case UserEntryType::ROOT_ACCESS_DENIED:
                case UserEntryType::ANON_PROXY_ACCESS_DENIED:
                    error = AuthErrorType::ACCESS_DENIED;
                    break;

                case UserEntryType::BAD_DB:
                    error = AuthErrorType::BAD_DB;
                    break;

                default:
                    mxb_assert(!true);
                }
                send_authentication_error(error, auth_val.msg);
                m_auth_state = AuthState::FAIL;
            }
        }
        else
        {
            if (auth_val.status == AuthRes::Status::FAIL_WRONG_PW)
            {
                // Again, this may be because user data is obsolete. Update userdata, but fail
                // session anyway since I/O with client cannot be redone.
                m_session->service->request_user_account_update();
            }
            // This is also sent if the auth module fails.
            send_authentication_error(AuthErrorType::ACCESS_DENIED, auth_val.msg);
            m_auth_state = AuthState::FAIL;
        }
    }

    if (m_auth_state == AuthState::FAIL)
    {
        // Add only the true authentication failures into listener's host blocking counters. This way internal
        // reasons (e.g. no valid master found) don't trigger blocking of hosts.
        mxs::mark_auth_as_failed(m_dcb->remote());
    }
}

bool MariaDBClientConnection::in_routing_state() const
{
    return m_state == State::READY;
}

json_t* MariaDBClientConnection::diagnostics() const
{
    return json_pack("{ss}", "cipher", m_dcb->ssl_cipher().c_str());
}

bool MariaDBClientConnection::large_query_continues(const mxs::Buffer& buffer) const
{
    return MYSQL_GET_PACKET_LEN(buffer.get()) == MAX_PACKET_SIZE;
}

bool MariaDBClientConnection::process_normal_packet(mxs::Buffer&& buffer)
{
    bool success = false;
    track_current_command(buffer);
    bool is_large = large_query_continues(buffer);
    if (m_command == MXS_COM_CHANGE_USER)
    {
        update_sequence(buffer.get());
        m_session_data->next_sequence = m_sequence + 1;

        // Client sent a change-user-packet. Parse it but only route it once change-user completes.
        if (start_change_user(move(buffer)))
        {
            m_state = State::CHANGING_USER;
            m_auth_state = AuthState::FIND_ENTRY;
            m_dcb->trigger_read_event();
            success = true;
        }
    }
    else
    {
        bool routed = route_statement(move(buffer));
        if (routed && is_large)
        {
            m_routing_state = RoutingState::LARGE_PACKET;
        }
        success = routed;
    }
    return success;
}

void MariaDBClientConnection::write_ok_packet(int sequence, uint8_t affected_rows, const char* message)
{
    write(mxs_mysql_create_ok(sequence, affected_rows, message));
}

bool MariaDBClientConnection::send_mysql_err_packet(int packet_number, int in_affected_rows,
                                                    int mysql_errno, const char* sqlstate_msg,
                                                    const char* mysql_message)
{
    GWBUF* buf = modutil_create_mysql_err_msg(packet_number, in_affected_rows, mysql_errno,
                                              sqlstate_msg, mysql_message);
    return write(buf);
}

void MariaDBClientConnection::add_local_client(LocalClient* client)
{
    // Prune stale LocalClients before adding the new one
    auto it = std::remove_if(m_local_clients.begin(), m_local_clients.end(), [](const auto& client) {
                                 return !client->is_open();
                             });

    m_local_clients.erase(it, m_local_clients.end());

    m_local_clients.emplace_back(client);
}

void MariaDBClientConnection::kill()
{
    m_local_clients.clear();
}
