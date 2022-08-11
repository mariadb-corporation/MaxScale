/*
 *
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
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
#include <grp.h>
#include <pwd.h>

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
#include <maxscale/protocol/mariadb/query_classifier.hh>
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
#include "detect_special_query.hh"

namespace
{
using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using ExcRes = mariadb::ClientAuthenticator::ExchRes;
using UserEntryType = mariadb::UserEntryType;
using TrxState = MYSQL_session::TrxState;
using std::move;
using std::string;

const string base_plugin = DEFAULT_MYSQL_AUTH_PLUGIN;
const mxs::ListenerData::UserCreds default_mapped_creds = {"", base_plugin};
const int CLIENT_CAPABILITIES_LEN = 32;
const int SSL_REQUEST_PACKET_SIZE = MYSQL_HEADER_LEN + CLIENT_CAPABILITIES_LEN;
const int NORMAL_HS_RESP_MIN_SIZE = MYSQL_AUTH_PACKET_BASE_SIZE + 2;
const int MAX_PACKET_SIZE = MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN;

// The past-the-end value for the session command IDs we generate (includes prepared statements). When this ID
// value is reached, the counter is reset back to 1. This makes sure we reserve the values 0 and 0xffffffff as
// special values that are never assigned by MaxScale.
const uint32_t MAX_SESCMD_ID = std::numeric_limits<uint32_t>::max();
static_assert(MAX_SESCMD_ID == MARIADB_PS_DIRECT_EXEC_ID, "MAX_SESCMD_ID should equal to MARIADB_PS_DIRECT_EXEC_ID");

// Default version string sent to clients
const string default_version = "5.5.5-10.2.12 " MAXSCALE_VERSION "-maxscale";

class ThisUnit
{
public:
    mxb::Regex special_queries_regex;
};
ThisUnit this_unit;

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

std::pair<CapTypes, uint64_t> get_supported_cap_types(SERVICE* service)
{
    CapTypes type = CapTypes::MARIADB;
    uint64_t version = std::numeric_limits<uint64_t>::max();

    for (SERVER* s : service->reachable_servers())
    {
        if (s->info().type() == SERVER::VersionInfo::Type::XPAND)
        {
            // At least one node is XPand and since it's the most restrictive, we can return early.
            type = CapTypes::XPAND;
            break;
        }
        else
        {
            version = std::min(s->info().version_num().total, version);

            if (version < 100200)
            {
                type = CapTypes::NORMAL;
            }
        }
    }

    return {type, version};
}

mariadb::HeaderData parse_header(GWBUF* buffer)
{
    mariadb::HeaderData rval;
    if (gwbuf_link_length(buffer) >= MYSQL_HEADER_LEN)
    {
        // Header in first chunk.
        rval = mariadb::get_header(gwbuf_link_data(buffer));
    }
    else
    {
        // The header is split between multiple chunks.
        uint8_t header[MYSQL_HEADER_LEN];
        gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, header);
        rval = mariadb::get_header(header);
    }
    return rval;
}

bool call_getpwnam_r(const char* user, gid_t& group_id_out)
{
    bool rval = false;
    // getpwnam_r requires a buffer for result data. The size is not known beforehand. Guess the size and
    // try again with a larger buffer if necessary.
    int buf_size = 1024;
    const int buf_size_limit = 1024000;
    const char err_msg[] = "'getpwnam_r' on '%s' failed. Error %i: %s";
    string buffer;
    passwd output {};
    passwd* output_ptr = nullptr;
    bool keep_trying = true;

    while (buf_size <= buf_size_limit && keep_trying)
    {
        keep_trying = false;
        buffer.resize(buf_size);
        int ret = getpwnam_r(user, &output, &buffer[0], buffer.size(), &output_ptr);

        if (output_ptr)
        {
            group_id_out = output_ptr->pw_gid;
            rval = true;
        }
        else if (ret == 0)
        {
            // No entry found, likely the user is not a Linux user.
            MXB_INFO("Tried to check groups of user '%s', but it is not a Linux user.", user);
        }
        else if (ret == ERANGE)
        {
            // Buffer was too small. Try again with a larger one.
            buf_size *= 10;
            if (buf_size > buf_size_limit)
            {
                MXB_ERROR(err_msg, user, ret, mxb_strerror(ret));
            }
            else
            {
                keep_trying = true;
            }
        }
        else
        {
            MXB_ERROR(err_msg, user, ret, mxb_strerror(ret));
        }
    }
    return rval;
}

bool call_getgrgid_r(gid_t group_id, string& name_out)
{
    bool rval = false;
    // getgrgid_r requires a buffer for result data. The size is not known beforehand. Guess the size and
    // try again with a larger buffer if necessary.
    int buf_size = 1024;
    const int buf_size_limit = 1024000;
    const char err_msg[] = "'getgrgid_r' on %ui failed. Error %i: %s";
    string buffer;
    group output {};
    group* output_ptr = nullptr;
    bool keep_trying = true;

    while (buf_size <= buf_size_limit && keep_trying)
    {
        keep_trying = false;
        buffer.resize(buf_size);
        int ret = getgrgid_r(group_id, &output, &buffer[0], buffer.size(), &output_ptr);

        if (output_ptr)
        {
            name_out = output_ptr->gr_name;
            rval = true;
        }
        else if (ret == 0)
        {
            MXB_ERROR("Group id %ui is not a valid Linux group.", group_id);
        }
        else if (ret == ERANGE)
        {
            // Buffer was too small. Try again with a larger one.
            buf_size *= 10;
            if (buf_size > buf_size_limit)
            {
                MXB_ERROR(err_msg, group_id, ret, mxb_strerror(ret));
            }
            else
            {
                keep_trying = true;
            }
        }
        else
        {
            MXB_ERROR(err_msg, group_id, ret, mxb_strerror(ret));
        }
    }
    return rval;
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
        else if (mxb_log_should_log(LOG_INFO))
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
 * @return True on success
 */
bool MariaDBClientConnection::send_server_handshake()
{
    auto service = m_session->service;
    packet_parser::ByteVec payload;
    // The exact size depends on a few factors, reserve enough to avoid reallocations in most cases.
    payload.reserve(130);

    // Contents as in https://mariadb.com/kb/en/connection/#initial-handshake-packet
    payload.push_back((uint8_t)GW_MYSQL_PROTOCOL_VERSION);
    payload.push_back(get_version_string(service));

    // The length of the following fields all the way until plugin name is 44.
    const int id_to_plugin_bytes = 44;
    auto orig_size = payload.size();
    payload.resize(orig_size + id_to_plugin_bytes);
    auto ptr = payload.data() + orig_size;

    // Use the session id as the server thread id. Only the low 32bits are sent in the handshake.
    mariadb::set_byte4(ptr, m_session->id());
    ptr += 4;

    /* gen_random_bytes() generates random bytes (0-255). This is ok as scramble for most clients
     * (e.g. mariadb) but not for mysql-connector-java. To be on the safe side, ensure every byte
     * is a non-whitespace character. To do the rescaling of values without noticeable bias, generate
     * double the required bytes.
     */
    uint8_t random_bytes[2 * MYSQL_SCRAMBLE_LEN];
    mxb::Worker::gen_random_bytes(random_bytes, sizeof(random_bytes));
    auto* scramble_storage = m_session_data->scramble;
    for (size_t i = 0; i < MYSQL_SCRAMBLE_LEN; i++)
    {
        auto src = &random_bytes[2 * i];
        auto val16 = *(reinterpret_cast<uint16_t*>(src));
        scramble_storage[i] = '!' + (val16 % (('~' + 1) - '!'));
    }

    // Write scramble part 1.
    ptr = mariadb::copy_bytes(ptr, scramble_storage, 8);

    // Filler byte.
    *ptr++ = 0;

    // 8 bytes of capabilities, sent in three parts.
    uint64_t caps = GW_MYSQL_CAPABILITIES_SERVER;
    CapTypes cap_types;
    int min_version;
    std::tie(cap_types, min_version) = get_supported_cap_types(service);

    if (cap_types == CapTypes::MARIADB)
    {
        // A MariaDB 10.2 server or later omits the CLIENT_MYSQL capability. This signals that it supports
        // extended capabilities.
        caps &= ~GW_MYSQL_CAPABILITIES_CLIENT_MYSQL;
        caps |= MXS_EXTRA_CAPS_SERVER64;

        if (min_version < 100600)
        {
            // The metadata caching was added in 10.6 and should only be enabled if all nodes support it.
            caps &= ~(MXS_MARIA_CAP_CACHE_METADATA << 32);
            mxb_assert((caps & MXS_EXTRA_CAPS_SERVER64) == (MXS_MARIA_CAP_STMT_BULK_OPERATIONS << 32));
        }
    }

    if (m_session->capabilities() & RCAP_TYPE_OLD_PROTOCOL)
    {
        // Some module requires that only the base protocol is used, most likely due to the fact
        // that it processes the contents of the resultset.
        caps &= ~((MXS_MARIA_CAP_CACHE_METADATA << 32) | GW_MYSQL_CAPABILITIES_DEPRECATE_EOF);
        mxb_assert((caps & MXS_EXTRA_CAPS_SERVER64) == (MXS_MARIA_CAP_STMT_BULK_OPERATIONS << 32)
                   || cap_types != CapTypes::MARIADB);
        mxb_assert((caps & GW_MYSQL_CAPABILITIES_DEPRECATE_EOF) == 0);
    }

    if (cap_types == CapTypes::XPAND || min_version < 80000 || (min_version > 100000 && min_version < 100208))
    {
        // The DEPRECATE_EOF and session tracking were added in MySQL 5.7, anything older than that shouldn't
        // advertise them. This includes XPand: it doesn't support SESSION_TRACK or DEPRECATE_EOF as it's
        // MySQL 5.1 compatible on the protocol layer. Additionally, MySQL 5.7 has a broken query cache
        // implementation where it sends non-DEPRECATE_EOF results even when a client requested results in the
        // DEPRECATE_EOF format. The same query cache bug was present in MariaDB but was fixed in 10.2.8
        // (MDEV-13300).
        caps &= ~(GW_MYSQL_CAPABILITIES_SESSION_TRACK | GW_MYSQL_CAPABILITIES_DEPRECATE_EOF);
    }

    if (require_ssl())
    {
        caps |= GW_MYSQL_CAPABILITIES_SSL;
    }

    m_session_data->client_caps.advertised_capabilities = caps;

    // Convert to little endian, write 2 bytes.
    uint8_t caps_le[8];
    mariadb::set_byte8(caps_le, caps);
    ptr = mariadb::copy_bytes(ptr, caps_le, 2);

    // Character set.
    uint8_t charset = service->charset();
    if (charset == 0)
    {
        charset = 8;        // Charset 8 is latin1, the server default.
    }
    *ptr++ = charset;

    uint16_t status_flags = 2;      // autocommit enabled
    mariadb::set_byte2(ptr, status_flags);
    ptr += 2;

    // More capabilities.
    ptr = mariadb::copy_bytes(ptr, caps_le + 2, 2);

    *ptr++ = MYSQL_SCRAMBLE_LEN + 1;    // Plugin data total length, contains 1 filler.

    // 6 bytes filler
    ptr = mariadb::set_bytes(ptr, 0, 6);

    // Capabilities part 3 or 4 filler bytes.
    ptr = cap_types == CapTypes::MARIADB ?
        mariadb::copy_bytes(ptr, caps_le + 4, 4) :
        mariadb::set_bytes(ptr, 0, 4);

    // Scramble part 2.
    ptr = mariadb::copy_bytes(ptr, scramble_storage + 8, 12);

    // filler
    *ptr++ = 0;

    mxb_assert(ptr - (payload.data() + orig_size) == id_to_plugin_bytes);
    // Add plugin name.
    payload.push_back(base_plugin);

    bool rval = false;
    // Allocate buffer and send.
    auto pl_size = payload.size();
    GWBUF* buf = gwbuf_alloc(MYSQL_HEADER_LEN + pl_size);
    if (buf)
    {
        ptr = GWBUF_DATA(buf);
        ptr = mariadb::write_header(ptr, pl_size, 0);
        memcpy(ptr, payload.data(), pl_size);
        rval = (write(buf) == 1);
    }
    return rval;
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
    auto& auth_data = (auth_type == AuthType::NORMAL_AUTH) ? *m_session_data->auth_data :
        *m_change_user.auth_data;
    const auto& user_entry_type = auth_data.user_entry.type;

    while (state_machine_continue)
    {
        switch (m_auth_state)
        {
        case AuthState::FIND_ENTRY:
            {
                update_user_account_entry(auth_data);
                if (user_entry_type == UserEntryType::USER_ACCOUNT_OK)
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
                        MXS_WARNING(USERS_RECENTLY_UPDATED_FMT, m_session_data->user_and_host().c_str());
                        // If plugin exists, start exchange. Authentication will surely fail.
                        m_auth_state = (user_entry_type == UserEntryType::PLUGIN_IS_NOT_LOADED) ?
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
                        update_user_account_entry(auth_data);
                    }

                    if (user_entry_type == UserEntryType::USER_ACCOUNT_OK)
                    {
                        MXB_DEBUG("Found user account entry for %s after updating user account data.",
                                  m_session_data->user_and_host().c_str());
                    }
                    m_auth_state = (user_entry_type == UserEntryType::PLUGIN_IS_NOT_LOADED) ?
                        AuthState::NO_PLUGIN : AuthState::START_EXCHANGE;
                }
                else
                {
                    // Should not get client data (or read events) before users have actually been updated.
                    // This can happen if client hangs up while MaxScale is waiting for the update.
                    MXB_ERROR("Client %s sent data when waiting for user account update. Closing session.",
                              m_session_data->user_and_host().c_str());
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
            state_machine_continue = perform_auth_exchange(auth_data);
            break;

        case AuthState::CHECK_TOKEN:
            perform_check_token(auth_type);
            break;

        case AuthState::START_SESSION:
            // Authentication success, initialize session. Backend authenticator must be set before
            // connecting to backends.
            m_session_data->current_db = auth_data.default_db;
            m_session_data->role = auth_data.user_entry.entry.default_role;
            assign_backend_authenticator(auth_data);
            if (m_session->start())
            {
                mxb_assert(m_session->state() != MXS_SESSION::State::CREATED);
                m_auth_state = AuthState::COMPLETE;
            }
            else
            {
                // Send internal error, as in this case the client has done nothing wrong.
                send_mysql_err_packet(1815, "HY000", "Internal error: Session creation failed");
                MXB_ERROR("Failed to create session for %s.", m_session_data->user_and_host().c_str());
                m_auth_state = AuthState::FAIL;
            }
            break;

        case AuthState::CHANGE_USER_OK:
            {
                // Reauthentication to MaxScale succeeded, but the query still needs to be successfully
                // routed.
                rval = complete_change_user_p1() ? StateMachineRes::DONE : StateMachineRes::ERROR;
                state_machine_continue = false;
                break;
            }

        case AuthState::COMPLETE:
            m_sql_mode = m_session->listener_data()->m_default_sql_mode;
            write_ok_packet(m_next_sequence);
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
                cancel_change_user_p1();
                rval = StateMachineRes::DONE;
            }

            break;
        }
    }
    return rval;
}

void MariaDBClientConnection::update_user_account_entry(mariadb::AuthenticationData& auth_data)
{
    const auto mses = m_session_data;
    auto* users = user_account_cache();
    auto search_res = users->find_user(auth_data.user, mses->remote, auth_data.default_db,
                                       mses->user_search_settings);
    m_previous_userdb_version = users->version();   // Can use this to skip user entry check after update.

    mariadb::AuthenticatorModule* selected_module = find_auth_module(search_res.entry.plugin);
    if (selected_module)
    {
        // Correct plugin is loaded, generate session-specific data.
        auth_data.client_auth_module = selected_module;
        // If changing user, this overrides the old client authenticator. Not an issue, as the client auth
        // is only used during authentication.
        m_authenticator = selected_module->create_client_authenticator();
    }
    else
    {
        // Authentication cannot continue in this case. Should be rare, though.
        search_res.type = UserEntryType::PLUGIN_IS_NOT_LOADED;
        MXB_INFO("User entry '%s'@'%s' uses unrecognized authenticator plugin '%s'. "
                 "Cannot authenticate user.",
                 search_res.entry.username.c_str(), search_res.entry.host_pattern.c_str(),
                 search_res.entry.plugin.c_str());
    }
    auth_data.user_entry = move(search_res);
}

/**
 * Handle relevant variables
 *
 * @param buffer  Buffer, assumed to contain a statement. May be reallocated if not contiguous.
 *
 * @return NULL if successful, otherwise dynamically allocated error message.
 */
char* MariaDBClientConnection::handle_variables(mxs::Buffer& buffer)
{
    char* message = NULL;
    auto read_buffer = buffer.release();
    SetParser set_parser;
    SetParser::Result result;

    switch (set_parser.check(&read_buffer, &result))
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
                    m_session_data->is_autocommit = false;
                    m_sql_mode = QC_SQL_MODE_ORACLE;
                    break;

                case SqlModeParser::DEFAULT:
                    m_session_data->is_autocommit = true;
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
                message = m_session->set_variable_value(variable.first, variable.second,
                                                        value.first, value.second);
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

    buffer.reset(read_buffer);

    return message;
}

void MariaDBClientConnection::track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf)
{
    auto& ses_trx_state = m_session_data->trx_state;
    const auto trx_starting_active = TrxState::TRX_ACTIVE | TrxState::TRX_STARTING;

    mxb_assert(gwbuf_is_contiguous(packetbuf));
    mxb_assert((ses_trx_state & (TrxState::TRX_STARTING | TrxState::TRX_ENDING))
               != (TrxState::TRX_STARTING | TrxState::TRX_ENDING));


    if (ses_trx_state & TrxState::TRX_ENDING)
    {
        if (m_session_data->is_autocommit)
        {
            // Transaction ended, go into inactive state
            ses_trx_state = TrxState::TRX_INACTIVE;
        }
        else
        {
            // Without autocommit the end of a transaction starts a new one
            ses_trx_state = trx_starting_active;
        }
    }
    else if (ses_trx_state & TrxState::TRX_STARTING)
    {
        ses_trx_state &= ~TrxState::TRX_STARTING;
    }
    else if (!m_session_data->is_autocommit && ses_trx_state == TrxState::TRX_INACTIVE)
    {
        // This state is entered when autocommit was disabled
        ses_trx_state = trx_starting_active;
    }

    if (mxs_mysql_get_command(packetbuf) == MXS_COM_QUERY)
    {
        const auto parser_type = rcap_type_required(
            m_session->capabilities(), RCAP_TYPE_QUERY_CLASSIFICATION) ?
            QC_TRX_PARSE_USING_QC : QC_TRX_PARSE_USING_PARSER;

        uint32_t type = qc_get_trx_type_mask_using(packetbuf, parser_type);

        if (type & QUERY_TYPE_BEGIN_TRX)
        {
            if (type & QUERY_TYPE_DISABLE_AUTOCOMMIT)
            {
                // This disables autocommit and the next statement starts a new transaction
                m_session_data->is_autocommit = false;
                ses_trx_state = TrxState::TRX_INACTIVE;
            }
            else
            {
                auto new_trx_state = trx_starting_active;
                if (type & QUERY_TYPE_READ)
                {
                    new_trx_state |= TrxState::TRX_READ_ONLY;
                }
                ses_trx_state = new_trx_state;
            }
        }
        else if (type & (QUERY_TYPE_COMMIT | QUERY_TYPE_ROLLBACK))
        {
            auto new_trx_state = ses_trx_state | TrxState::TRX_ENDING;
            // A commit never starts a new transaction. This would happen with: SET AUTOCOMMIT=0; COMMIT;
            new_trx_state &= ~TrxState::TRX_STARTING;
            ses_trx_state = new_trx_state;

            if (type & QUERY_TYPE_ENABLE_AUTOCOMMIT)
            {
                m_session_data->is_autocommit = true;
            }
        }
    }
}

void MariaDBClientConnection::handle_query_kill(const SpecialQueryDesc& kill_contents)
{
    auto kt = kill_contents.kill_options;
    auto& user = kill_contents.target;
    // TODO: handle "query id" somehow
    if ((kt & KT_QUERY_ID) == 0)
    {
        if (kill_contents.kill_id > 0)
        {
            execute_kill_connection(kill_contents.kill_id, (kill_type_t)kt);
        }
        else if (!user.empty())
        {
            execute_kill_user(user.c_str(), (kill_type_t)kt);
        }
        else
        {
            write_ok_packet(1);
        }
    }
}

MariaDBClientConnection::SpecialQueryDesc
MariaDBClientConnection::parse_kill_query_elems(const char* sql)
{
    const string connection = "connection";
    const string query = "query";
    const string hard = "hard";
    const string soft = "soft";

    auto& regex = this_unit.special_queries_regex;

    auto option = mxb::tolower(regex.substring_by_name(sql, "koption"));
    auto type = mxb::tolower(regex.substring_by_name(sql, "ktype"));
    auto target = mxb::tolower(regex.substring_by_name(sql, "ktarget"));

    SpecialQueryDesc rval;
    rval.type = SpecialQueryDesc::Type::KILL;

    // Option is either "hard", "soft", or empty.
    if (option == hard)
    {
        rval.kill_options |= KT_HARD;
    }
    else if (option == soft)
    {
        rval.kill_options |= KT_SOFT;
    }
    else
    {
        mxb_assert(option.empty());
    }

    // Type is either "connection", "query", "query\s+id" or empty.
    if (type == connection)
    {
        rval.kill_options |= KT_CONNECTION;
    }
    else if (type == query)
    {
        rval.kill_options |= KT_QUERY;
    }
    else if (!type.empty())
    {
        mxb_assert(type.find(query) == 0);
        rval.kill_options |= KT_QUERY_ID;
    }

    // target is either a query/thread id or "user\s+<username>"
    if (isdigit(target[0]))
    {
        mxb::get_uint64(target.c_str(), &rval.kill_id);
    }
    else
    {
        auto words = mxb::strtok(target, " ");
        rval.target = words[1];
    }
    return rval;
}

void MariaDBClientConnection::handle_use_database(GWBUF* read_buffer)
{
    auto databases = qc_get_database_names(read_buffer);
    if (!databases.empty())
    {
        start_change_db(move(databases[0]));
    }
}

/**
 * Some SQL commands/queries need to be detected and handled by the protocol
 * and MaxScale instead of being routed forward as is.
 *
 * @param buffer Query buffer
 * @return see @c spec_com_res_t
 */
MariaDBClientConnection::SpecialCmdRes
MariaDBClientConnection::process_special_queries(mxs::Buffer& buffer)
{
    auto rval = SpecialCmdRes::CONTINUE;

    auto packet_len = buffer.length();
    /* The packet must be at least HEADER + cmd + 5 (USE d) chars in length. Also, if the packet is rather
     * long, assume that it is not a tracked query. This assumption allows avoiding the
     * make_contiquous-call
     * on e.g. big inserts. The long packets can only contain one of the tracked queries by having lots of
     * comments. */
    const size_t min_len = MYSQL_HEADER_LEN + 1 + 5;
    const size_t max_len = 10000;

    if (packet_len >= min_len && packet_len <= max_len)
    {
        const char* sql = nullptr;
        int len = 0;

        buffer.make_contiguous();

        bool is_special = false;

        if (modutil_extract_SQL(buffer.get(), const_cast<char**>(&sql), &len))
        {
            auto pEnd = sql + len;
            is_special = detect_special_query(&sql, pEnd);
            len = pEnd - sql;
        }

        if (is_special)
        {
            auto fields = parse_special_query(sql, len);
            switch (fields.type)
            {
            case SpecialQueryDesc::Type::NONE:
                break;

            case SpecialQueryDesc::Type::KILL:
                handle_query_kill(fields);
                // The kill-query is not routed to backends, as the id:s would be wrong.
                rval = SpecialCmdRes::END;
                break;

            case SpecialQueryDesc::Type::USE_DB:
                handle_use_database(buffer.get());
                break;

            case SpecialQueryDesc::Type::SET_ROLE:
                start_change_role(move(fields.target));
                break;
            }
        }
    }

    return rval;
}

bool MariaDBClientConnection::record_for_history(mxs::Buffer& buffer, uint8_t cmd)
{
    bool should_record = false;
    const auto current_target = mariadb::QueryClassifier::CURRENT_TARGET_UNDEFINED;

    // Update the routing information. This must be done even if the command isn't added to the history.
    const auto& info = m_qc.update_route_info(current_target, buffer.get());

    switch (cmd)
    {
    case MXS_COM_QUIT:      // The client connection is about to be closed
    case MXS_COM_PING:      // Doesn't change the state so it doesn't need to be stored
    case MXS_COM_STMT_RESET:// Resets the prepared statement state, not needed by new connections
        break;

    case MXS_COM_STMT_EXECUTE:
        {
            uint32_t id = mxs_mysql_extract_ps_id(buffer.get());
            uint16_t params = m_qc.get_param_count(id);

            if (params > 0)
            {
                size_t types_offset = MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + ((params + 7) / 8);
                uint8_t* ptr = buffer.data() + types_offset;

                if (*ptr)
                {
                    ++ptr;

                    // Store the metadata, two bytes per parameter, for later use. The backends will need if
                    // it they have to reconnect and re-executed the prepared statements.
                    m_session_data->exec_metadata[id].assign(ptr, ptr + (params * 2));
                }
            }
        }
        break;

    case MXS_COM_STMT_CLOSE:
        {
            // Instead of handling COM_STMT_CLOSE like a normal command, we can exclude it from the history as
            // well as remove the original COM_STMT_PREPARE that it refers to. This simplifies the history
            // replay as all stored commands generate a response and none of them refer to any previous
            // commands. This means that the history can be executed in a single batch without waiting for any
            // responses.
            uint32_t id = mxs_mysql_extract_ps_id(buffer.get());

            auto it = std::find_if(m_session_data->history.begin(),
                                   m_session_data->history.end(),
                                   [&](const auto& a) {
                                       return a.id() == id;
                                   });

            if (it != m_session_data->history.end())
            {
                mxb_assert(it->id());
                m_session_data->history.erase(it);
                m_qc.ps_erase(buffer.get());
                m_session_data->exec_metadata.erase(id);
            }
        }
        break;

    case MXS_COM_CHANGE_USER:
        // COM_CHANGE_USER resets the whole connection. Any new connections will already be using the new
        // credentials which means we can safely reset the history here.
        m_session_data->history.clear();
        break;

    case MXS_COM_STMT_PREPARE:
        should_record = true;
        break;

    default:
        should_record = m_qc.target_is_all(info.target());
        break;
    }

    if (should_record)
    {
        buffer.set_id(m_next_id);
        m_pending_cmd = buffer;     // Keep a copy for the session command history
        should_record = true;

        if (cmd == MXS_COM_STMT_PREPARE || qc_query_is_type(info.type_mask(), QUERY_TYPE_PREPARE_NAMED_STMT))
        {
            // This will silence the warnings about unknown PS IDs
            m_qc.ps_store(buffer.get(), m_next_id);
        }

        if (++m_next_id == MAX_SESCMD_ID)
        {
            m_next_id = 1;
        }
    }

    return should_record;
}

/**
 * Route an SQL protocol packet. If the original client packet is less than 16MB, buffer should
 * contain the complete packet. If the client packet is large (split into multiple protocol packets),
 * only one protocol packet should be routed at a time.
 * TODO: what happens with parsing in this case? Likely it fails.
 *
 * @param buffer Buffer to route
 * @return True on success
 */
bool MariaDBClientConnection::route_statement(mxs::Buffer&& buffer)
{
    bool recording = false;
    uint8_t cmd = mxs_mysql_get_command(buffer.get());

    buffer.make_contiguous();

    if (m_session->capabilities() & RCAP_TYPE_SESCMD_HISTORY)
    {
        recording = record_for_history(buffer, cmd);
    }
    else if (cmd == MXS_COM_STMT_PREPARE)
    {
        buffer.set_id(m_next_id);

        if (++m_next_id == MAX_SESCMD_ID)
        {
            m_next_id = 1;
        }
    }

    // Must be done whether or not there were any changes, as the query classifier
    // is thread and not session specific.
    qc_set_sql_mode(m_sql_mode);
    // The query classifier classifies according to the service's server that has
    // the smallest version number.
    qc_set_server_version(m_version);

    auto service = m_session->service;
    auto capabilities = m_session->capabilities();

    if (rcap_type_required(capabilities, RCAP_TYPE_TRANSACTION_TRACKING)
        && !service->config()->session_track_trx_state && !m_session->load_active)
    {
        track_transaction_state(m_session, buffer.get());
    }

    // TODO: The response count and state is currently modified before we route the query to allow routers to
    // call clientReply inside routeQuery. This should be changed so that routers don't directly call
    // clientReply and instead it to be delivered in a separate event.

    bool expecting_response = mxs_mysql_command_will_respond(cmd);

    if (expecting_response)
    {
        ++m_num_responses;
        m_session->retain_statement(buffer.get());
    }

    if (recording)
    {
        mxb_assert(expecting_response);
        m_routing_state = RoutingState::RECORD_HISTORY;
    }

    return m_downstream->routeQuery(buffer.release()) != 0;
}

void MariaDBClientConnection::finish_recording_history(const GWBUF* buffer, const mxs::Reply& reply)
{
    if (reply.is_complete())
    {
        MXS_INFO("Added %s to history with ID %u: %s (result: %s)",
                 STRPACKETTYPE(m_pending_cmd.data()[4]), m_pending_cmd.id(),
                 mxs::extract_sql(m_pending_cmd, 200).c_str(),
                 reply.is_ok() ? "OK" : reply.error().message().c_str());

        if (reply.command() == MXS_COM_STMT_PREPARE)
        {
            m_qc.ps_store_response(m_pending_cmd.id(), reply.param_count());
        }

        m_routing_state = RoutingState::COMPARE_RESPONSES;
        m_dcb->trigger_read_event();
        m_session_data->history_responses.emplace(m_pending_cmd.id(), reply.is_ok());
        m_session_data->history.emplace_back(m_pending_cmd.release());

        if (m_session_data->history.size() > m_max_sescmd_history)
        {
            prune_history();
        }
    }
}

void MariaDBClientConnection::prune_history()
{
    // Using the about-to-be-pruned command as the minimum ID prevents the removal of responses that are still
    // needed when the ID overflows. If only the stored positions were used, the whole history would be
    // cleared.
    uint32_t min_id = m_session_data->history.front().id();

    for (const auto& kv : m_session_data->history_info)
    {
        if (kv.second.position > 0 && kv.second.position < min_id)
        {
            min_id = kv.second.position;
        }
    }

    m_session_data->history_responses.erase(m_session_data->history_responses.begin(),
                                            m_session_data->history_responses.lower_bound(min_id));
    m_session_data->history.pop_front();
    m_session_data->history_pruned = true;
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

    if (m_routing_state == RoutingState::CHANGING_DB
        || m_routing_state == RoutingState::CHANGING_ROLE
        || m_routing_state == RoutingState::CHANGING_USER
        || m_routing_state == RoutingState::RECORD_HISTORY)
    {
        // We're still waiting for a response from the backend, read more data once we get it.
        return StateMachineRes::IN_PROGRESS;
    }
    else if (m_routing_state == RoutingState::COMPARE_RESPONSES)
    {
        // A session command that was recorded was just processed. Call the installed callbacks for any
        // backends that responded before the accepted response was received.
        for (auto& kv : m_session_data->history_info)
        {
            if (auto cb = kv.second.response_cb)
            {
                kv.second.response_cb = nullptr;
                cb();
            }
        }

        m_routing_state = RoutingState::PACKET_START;
    }

    auto read_res = read_protocol_packet();
    mxs::Buffer buffer = move(read_res.data);
    if (read_res.error())
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
            if (rcap_type_required(m_session->capabilities(), RCAP_TYPE_STMT_INPUT))
            {
                buffer.make_contiguous();
            }

            // No command bytes, just continue routing large packet.
            bool is_large = large_query_continues(buffer);
            routed = m_downstream->routeQuery(buffer.release()) != 0;

            if (!is_large)
            {
                // Large packet routing completed.
                m_routing_state = RoutingState::PACKET_START;
            }
        }
        break;

    case RoutingState::LARGE_HISTORY_PACKET:
        {
            // A continuation of a recoded command, append it to the current command and route it forward
            m_pending_cmd.append(gwbuf_clone(buffer.get()));
            bool is_large = large_query_continues(buffer);
            routed = m_downstream->routeQuery(buffer.release()) != 0;

            if (!is_large)
            {
                // Large packet routing completed.
                m_routing_state = RoutingState::RECORD_HISTORY;
                mxb_assert(m_pending_cmd.length() > MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN);
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

    case RoutingState::CHANGING_DB:
    case RoutingState::CHANGING_ROLE:
    case RoutingState::CHANGING_USER:
    case RoutingState::RECORD_HISTORY:
    case RoutingState::COMPARE_RESPONSES:
        mxb_assert_message(!true, "We should never end up here");
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
        mxb_assert_message(m_session->normal_quit(), "Session should be quitting normally");
        m_state = State::QUIT;
        rval = StateMachineRes::DONE;
    }

    return rval;
}

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
                    if (auth_type == AuthType::NORMAL_AUTH)
                    {
                        // Allow pooling for fresh sessions. This allows pooling in situations where
                        // the client/connector does not send any queries at start and session stays idle.
                        m_session->set_can_pool_backends(true);
                    }
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

    if (!m_session->normal_quit())
    {
        if (session_get_dump_statements() == SESSION_DUMP_STATEMENTS_ON_ERROR)
        {
            m_session->dump_statements();
        }

        if (session_get_session_trace())
        {
            m_session->dump_session_log();
        }

        // The client did not send a COM_QUIT packet
        std::string errmsg {"Connection killed by MaxScale"};
        std::string extra {session_get_close_reason(m_session)};

        if (!extra.empty())
        {
            errmsg += ": " + extra;
        }

        send_mysql_err_packet(1927, "08S01", errmsg.c_str());
    }

    // We simply close the session, this will propagate the closure to any
    // backend descriptors and perform the session cleanup.
    m_session->kill();
}

bool MariaDBClientConnection::init_connection()
{
    return send_server_handshake();
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
    , m_qc(this, session, TYPE_ALL, mariadb::QueryClassifier::Log::NONE)
{
    const auto& svc_config = *m_session->service->config();
    m_max_sescmd_history = svc_config.disable_sescmd_history ? 0 : svc_config.max_sescmd_history;
    m_track_pooling_status = session->idle_pooling_enabled();
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
    const int mysql_errno = 1045;
    mariadb::set_byte2(mysql_err, mysql_errno);
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

void MariaDBClientConnection::execute_kill(std::shared_ptr<KillInfo> info, bool send_ok)
{
    MXS_SESSION* ref = session_get_ref(m_session);
    auto origin = mxs::RoutingWorker::get_current();

    auto func = [this, info, ref, origin, send_ok]() {
        // First, gather the list of servers where the KILL should be sent
        mxs::RoutingWorker::execute_concurrently(
            [info, ref]() {
            dcb_foreach_local(info->cb, info.get());
        });

        // TODO: This doesn't handle the case where a session is moved from one worker to another while
        // this was being executed on the MainWorker.

        // Then move execution back to the original worker to keep all connections on the same thread
        origin->call(
            [this, info, ref, origin, send_ok]() {
            for (const auto& a : info->targets)
            {
                std::unique_ptr<LocalClient> client(LocalClient::create(info->session, a.first));

                if (client)
                {
                    if (client->connect())
                    {
                        auto ok_cb = [this, send_ok, cl = client.get()](
                            GWBUF*, const mxs::ReplyRoute&, const mxs::Reply&){
                            kill_complete(send_ok, cl);
                        };
                        auto err_cb = [this, send_ok, cl = client.get()](
                            GWBUF*, mxs::Target*, const mxs::Reply&) {
                            kill_complete(send_ok, cl);
                        };

                        client->set_notify(std::move(ok_cb), std::move(err_cb));

                        // TODO: There can be multiple connections to the same server. Currently only
                        // one connection per server is killed.
                        MXB_INFO("KILL on '%s': %s", a.first->name(), a.second.c_str());

                        if (!client->queue_query(modutil_create_query(a.second.c_str())))
                        {
                            MXB_INFO("Failed to route all KILL queries to '%s'", a.first->name());
                        }
                        else
                        {
                            mxb_assert(ref->state() != MXS_SESSION::State::STOPPING);
                            add_local_client(client.release());
                        }
                    }
                    else
                    {
                        MXB_INFO("Failed to connect LocalClient to '%s'", a.first->name());
                    }
                }
                else
                {
                    MXB_INFO("Failed to create LocalClient to '%s'", a.first->name());
                }
            }

            // If we ended up not sending any KILL commands, the OK packet can be generated immediately.
            maybe_send_kill_response(send_ok);

            // The reference can now be freed as the execution is back on the worker that owns it
            session_put_ref(ref);
        }, mxs::RoutingWorker::EXECUTE_AUTO);
    };

    mxs::MainWorker::get()->execute(func, mxb::Worker::EXECUTE_QUEUED);
}

std::string kill_query_prefix(MariaDBClientConnection::kill_type_t type)
{
    using Type = MariaDBClientConnection::kill_type_t;
    const char* hard = (type & Type::KT_HARD) ? "HARD " : (type & Type::KT_SOFT) ? "SOFT " : "";
    const char* query = (type & Type::KT_QUERY) ? "QUERY " : "";
    std::stringstream ss;
    ss << "KILL " << hard << query;
    return ss.str();
}

void MariaDBClientConnection::mxs_mysql_execute_kill(uint64_t target_id, MariaDBClientConnection::kill_type_t type)
{
    auto str = kill_query_prefix(type);
    auto info = std::make_shared<ConnKillInfo>(target_id, str, m_session, 0);
    execute_kill(info, false);
}

/**
 * Send KILL to all but the keep_protocol_thread_id. If keep_protocol_thread_id==0, kill all.
 * TODO: The naming: issuer, target_id, protocol_thread_id is not very descriptive,
 *       and really goes to the heart of explaining what the session_id/thread_id means in terms
 *       of a service/server pipeline and the recursiveness of this call.
 */
void MariaDBClientConnection::execute_kill_connection(uint64_t target_id,
                                                      MariaDBClientConnection::kill_type_t type)
{
    auto str = kill_query_prefix(type);
    auto info = std::make_shared<ConnKillInfo>(target_id, str, m_session, 0);
    execute_kill(info, true);
}

void MariaDBClientConnection::execute_kill_user(const char* user, kill_type_t type)
{
    auto str = kill_query_prefix(type);
    str += "USER ";
    str += user;

    auto info = std::make_shared<UserKillInfo>(user, str, m_session);
    execute_kill(info, true);
}

std::string MariaDBClientConnection::current_db() const
{
    return m_session_data->current_db;
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
        auto res = packet_parser::parse_client_capabilities(data, m_session_data->client_caps);
        m_session_data->client_caps = res.capabilities;
        m_session_data->auth_data->collation = res.collation;
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

        auto client_info = packet_parser::parse_client_capabilities(data, m_session_data->client_caps);
        auto parse_res = packet_parser::parse_client_response(data,
                                                              client_info.capabilities.basic_capabilities);

        if (parse_res.success)
        {
            // If the buffer is valid, just one 0 should remain. Some (old) connectors may send malformed
            // packets with extra data. Such packets work, but some data may not be parsed properly.
            auto data_size = data.size();
            if (data_size >= 1)
            {
                // Success, save data to session.
                auto& auth_data = *m_session_data->auth_data;
                auth_data.user = move(parse_res.username);
                m_session->set_user(auth_data.user);
                auth_data.client_token = move(parse_res.token_res.auth_token);
                auth_data.default_db = move(parse_res.db);
                auth_data.plugin = move(parse_res.plugin);
                auth_data.collation = client_info.collation;

                // Discard the attributes if there is any indication of failed parsing, as the contents
                // may be garbled.
                if (parse_res.success && data_size == 1)
                {
                    auth_data.attributes = move(parse_res.attr_res.attr_data);
                }
                else
                {
                    client_info.capabilities.basic_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_ATTRS;
                }
                m_session_data->client_caps = client_info.capabilities;

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
    mariadb::HeaderData header;
    int buffer_len = m_dcb->read(&read_buffer, SSL_REQUEST_PACKET_SIZE);
    if (buffer_len >= MYSQL_HEADER_LEN)
    {
        header = parse_header(read_buffer);
    }
    else if (buffer_len >= 0)
    {
        // Didn't read enough, try again.
        m_dcb->readq_prepend(read_buffer);
        return true;
    }
    else
    {
        return false;
    }

    // Got the protocol packet length.
    bool rval = true;
    int prot_packet_len = header.pl_length + MYSQL_HEADER_LEN;
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
        if (read_buffer)
        {
            m_sequence = header.seq;
            m_next_sequence = header.seq + 1;
        }
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

bool MariaDBClientConnection::is_idle() const
{
    return in_routing_state();
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

                // Use alternate authentication data storage during change user processing. The effects are
                // not visible to the session. The client authenticator object does not need to be preserved.
                m_change_user.auth_data = std::make_unique<mariadb::AuthenticationData>();
                auto& auth_data = *m_change_user.auth_data;
                auth_data.user = move(parse_res.username);
                auth_data.default_db = move(parse_res.db);
                auth_data.plugin = move(parse_res.plugin);
                auth_data.collation = parse_res.charset;
                auth_data.client_token = move(parse_res.token_res.auth_token);
                auth_data.attributes = move(parse_res.attr_res.attr_data);

                rval = true;
                MXB_INFO("Client %s is attempting a COM_CHANGE_USER to '%s'.",
                         m_session_data->user_and_host().c_str(), auth_data.user.c_str());
            }
        }
        else if (parse_res.token_res.old_protocol)
        {
            MXB_ERROR("Client %s attempted a COM_CHANGE_USER with pre-4.1 authentication, "
                      "which is not supported.", m_session_data->user_and_host().c_str());
        }
    }
    return rval;
}

bool MariaDBClientConnection::complete_change_user_p1()
{
    // Change-user succeeded on client side. It must still be routed to backends and the reply needs to
    // be OK. Either can fail. First, backup current session authentication data, then overwrite it with
    // the change-user authentication data. Backend authenticators will read the new data.

    auto& curr_auth_data = m_session_data->auth_data;
    m_change_user.auth_data_bu = move(curr_auth_data);
    curr_auth_data = move(m_change_user.auth_data);

    assign_backend_authenticator(*curr_auth_data);

    bool rval = false;
    // Failure here means a comms error -> session failure.
    if (route_statement(move(m_change_user.client_query)))
    {
        m_routing_state = RoutingState::CHANGING_USER;
        rval = true;
    }
    return rval;
}

void MariaDBClientConnection::cancel_change_user_p1()
{
    MXB_INFO("COM_CHANGE_USER from '%s' to '%s' failed.",
             m_session_data->auth_data->user.c_str(), m_change_user.auth_data->user.c_str());
    // The main session fields have not been modified at this point, so canceling is simple.
    m_change_user.client_query.reset();
    m_change_user.auth_data.reset();
}

void MariaDBClientConnection::complete_change_user_p2()
{
    // At this point, the original auth data is in backup storage and the change-user data is "current".
    const auto& curr_auth_data = m_session_data->auth_data;
    const auto& orig_auth_data = m_change_user.auth_data_bu;

    if (curr_auth_data->user_entry.entry.super_priv && mxs::Config::get().log_warn_super_user)
    {
        MXB_WARNING("COM_CHANGE_USER from '%s' to super user '%s'.",
                    orig_auth_data->user.c_str(), curr_auth_data->user.c_str());
    }
    else
    {
        MXB_INFO("COM_CHANGE_USER from '%s' to '%s' succeeded.",
                 orig_auth_data->user.c_str(), curr_auth_data->user.c_str());
    }
    m_change_user.auth_data_bu.reset();     // No longer needed.
    m_session_data->current_db = curr_auth_data->default_db;
    m_session_data->role = curr_auth_data->user_entry.entry.default_role;
}

void MariaDBClientConnection::cancel_change_user_p2(GWBUF* buffer)
{
    auto& curr_auth_data = m_session_data->auth_data;
    auto& orig_auth_data = m_change_user.auth_data_bu;

    MXB_WARNING("COM_CHANGE_USER from '%s' to '%s' succeeded on MaxScale but "
                "returned (0x%0hhx) on backends: %s",
                orig_auth_data->user.c_str(), curr_auth_data->user.c_str(),
                mxs_mysql_get_command(buffer), mxs::extract_error(buffer).c_str());

    // Restore original auth data from backup.
    curr_auth_data = move(orig_auth_data);
}

MariaDBClientConnection::StateMachineRes MariaDBClientConnection::process_handshake()
{
    mxs::Buffer read_buffer;
    bool read_success;
    if (m_handshake_state == HSState::INIT)
    {
        // The first response from client requires special handling.
        read_success = read_first_client_packet(&read_buffer);
    }
    else
    {
        auto read_res = read_protocol_packet();
        read_buffer = move(read_res.data);
        read_success = !read_res.error();
    }

    if (!read_success)
    {
        return StateMachineRes::ERROR;
    }
    else if (read_buffer.empty())
    {
        // Not enough data was available yet.
        return StateMachineRes::IN_PROGRESS;
    }

    const char wrong_sequence[] = "Client (%s) sent packet with unexpected sequence number. "
                                  "Expected %i, got %i.";
    const char packets_ooo[] = "Got packets out of order";
    const char sql_errstate[] = "08S01";
    const int er_bad_handshake = 1043;
    const int er_out_of_order = 1156;

    auto buffer = read_buffer.get();
    auto rval = StateMachineRes::IN_PROGRESS;   // Returned to upper level SM
    bool state_machine_continue = true;

    while (state_machine_continue)
    {
        switch (m_handshake_state)
        {
        case HSState::INIT:
            m_handshake_state = require_ssl() ? HSState::EXPECT_SSL_REQ : HSState::EXPECT_HS_RESP;
            m_session_data->auth_data = std::make_unique<mariadb::AuthenticationData>();
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
                        send_mysql_err_packet(er_bad_handshake, sql_errstate,
                                              "Bad SSL handshake");
                        MXB_ERROR("Client (%s) sent an invalid SSLRequest.", m_dcb->remote().c_str());
                        m_handshake_state = HSState::FAIL;
                    }
                }
                else
                {
                    send_mysql_err_packet(er_out_of_order, sql_errstate, packets_ooo);
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
                    send_auth_error(m_next_sequence, "Access without SSL denied");
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
                        send_mysql_err_packet(er_bad_handshake, sql_errstate,
                                              "Bad handshake");
                        MXB_ERROR("Client (%s) sent an invalid HandShakeResponse.",
                                  m_session_data->remote.c_str());
                        m_handshake_state = HSState::FAIL;
                    }
                }
                else
                {
                    send_mysql_err_packet(er_out_of_order, sql_errstate, packets_ooo);
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

void MariaDBClientConnection::send_authentication_error(AuthErrorType error, const std::string& auth_mod_msg)
{
    auto ses = m_session_data;
    string mariadb_msg;
    const auto& auth_data = *ses->auth_data;

    switch (error)
    {
    case AuthErrorType::ACCESS_DENIED:
        mariadb_msg = mxb::string_printf("Access denied for user %s (using password: %s)",
                                         ses->user_and_host().c_str(),
                                         auth_data.client_token.empty() ? "NO" : "YES");
        send_mysql_err_packet(1045, "28000", mariadb_msg.c_str());
        break;

    case AuthErrorType::DB_ACCESS_DENIED:
        mariadb_msg = mxb::string_printf("Access denied for user %s to database '%s'",
                                         ses->user_and_host().c_str(), auth_data.default_db.c_str());
        send_mysql_err_packet(1044, "42000", mariadb_msg.c_str());
        break;

    case AuthErrorType::BAD_DB:
        mariadb_msg = mxb::string_printf("Unknown database '%s'", auth_data.default_db.c_str());
        send_mysql_err_packet(1049, "42000", mariadb_msg.c_str());
        break;

    case AuthErrorType::NO_PLUGIN:
        mariadb_msg = mxb::string_printf("Plugin '%s' is not loaded",
                                         auth_data.user_entry.entry.plugin.c_str());
        send_mysql_err_packet(1524, "HY000", mariadb_msg.c_str());
        break;
    }

    // Also log an authentication failure event.
    if (m_session->service->config()->log_auth_warnings)
    {
        string total_msg = mxb::string_printf("Authentication failed for user '%s'@[%s] to service '%s'. "
                                              "Originating listener: '%s'. MariaDB error: '%s'.",
                                              auth_data.user.c_str(), ses->remote.c_str(),
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
    send_mysql_err_packet(1105, "HY000", msg.c_str());
}

/**
 * Authentication exchange state for authenticator state machine.
 *
 * @return True, if the calling state machine should continue. False, if it should wait for more client data.
 */
bool MariaDBClientConnection::perform_auth_exchange(mariadb::AuthenticationData& auth_data)
{
    mxb_assert(m_auth_state == AuthState::START_EXCHANGE || m_auth_state == AuthState::CONTINUE_EXCHANGE);

    mxs::Buffer read_buffer;
    // Nothing to read on first exchange-call.
    if (m_auth_state == AuthState::CONTINUE_EXCHANGE)
    {
        auto read_res = read_protocol_packet();
        if (read_res)
        {
            read_buffer = move(read_res.data);
        }
        else if (read_res.error())
        {
            // Connection is likely broken, no need to send error message.
            m_auth_state = AuthState::FAIL;
            return true;
        }
        else
        {
            // Not enough data was available yet.
            return false;
        }
    }

    auto res = m_authenticator->exchange(read_buffer.get(), m_session_data, auth_data);
    if (!res.packet.empty())
    {
        res.packet.data()[MYSQL_SEQ_OFFSET] = m_next_sequence;
        write(res.packet.release());
    }

    bool state_machine_continue = true;
    if (res.status == ExcRes::Status::READY)
    {
        // Continue to password check.
        m_auth_state = AuthState::CHECK_TOKEN;
    }
    else if (res.status == ExcRes::Status::INCOMPLETE)
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
                                      auth_data.client_auth_module->name().c_str());
        send_misc_error(msg);
        m_auth_state = AuthState::FAIL;
    }
    return state_machine_continue;
}

void MariaDBClientConnection::perform_check_token(AuthType auth_type)
{
    // If the user entry didn't exist in the first place, don't check token and just fail.
    // TODO: server likely checks some random token to spend time, could add it later.
    auto& auth_data = authentication_data(auth_type);
    const auto& user_entry = auth_data.user_entry;
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
            auth_val = m_authenticator->authenticate(m_session_data, auth_data);
        }
        else
        {
            auth_val.status = AuthRes::Status::SUCCESS;
            // Need to copy the authentication tokens directly. The tokens should work as is for PAM and
            // GSSAPI.
            auth_data.backend_token = auth_data.client_token;
            auth_data.backend_token_2fa = auth_data.client_token_2fa;
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
            if (auth_val.status == AuthRes::Status::FAIL_WRONG_PW
                && user_account_cache()->can_update_immediately())
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
        m_session->service->stats().add_failed_auth();
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
    bool is_large = false;
    {
        const uint8_t* data = buffer.data();
        auto header = mariadb::get_header(data);
        m_command = MYSQL_GET_COMMAND(data);
        is_large = (header.pl_length == MYSQL_PACKET_LENGTH_MAX);
    }

    switch (m_command)
    {
    case MXS_COM_CHANGE_USER:
        // Client sent a change-user-packet. Parse it but only route it once change-user completes.
        if (start_change_user(move(buffer)))
        {
            m_state = State::CHANGING_USER;
            m_auth_state = AuthState::FIND_ENTRY;
            m_dcb->trigger_read_event();
            success = true;
        }
        break;

    case MXS_COM_QUIT:
        /** The client is closing the connection. We know that this will be the
         * last command the client sends so the backend connections are very likely
         * to be in an idle state.
         *
         * If the client is pipelining the queries (i.e. sending N request as
         * a batch and then expecting N responses) then it is possible that
         * the backend connections are not idle when the COM_QUIT is received.
         * In most cases we can assume that the connections are idle. */
        m_session->set_can_pool_backends(true);
        m_session->set_normal_quit();
        success = route_statement(move(buffer));
        break;

    case MXS_COM_SET_OPTION:
        /**
         * This seems to be only used by some versions of PHP.
         *
         * The option is stored as a two byte integer with the values 0 for enabling
         * multi-statements and 1 for disabling it.
         */
        {
            buffer.make_contiguous();
            auto& caps = m_session_data->client_caps.basic_capabilities;
            if (buffer.data()[MYSQL_HEADER_LEN + 2])
            {
                caps &= ~GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS;
            }
            else
            {
                caps |= GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS;
            }
            success = route_statement(move(buffer));
        }

        break;

    case MXS_COM_PROCESS_KILL:
        {
            buffer.make_contiguous();
            const uint8_t* data = buffer.data();
            uint64_t process_id = mariadb::get_byte4(data + MYSQL_HEADER_LEN + 1);
            execute_kill_connection(process_id, KT_CONNECTION);
            success = true;     // No further processing or routing.
        }
        break;

    case MXS_COM_INIT_DB:
        {
            buffer.make_contiguous();
            const uint8_t* data = buffer.data();
            auto start = data + MYSQL_HEADER_LEN + 1;
            auto end = data + buffer.length();
            start_change_db(string(start, end));
            success = route_statement(move(buffer));
        }
        break;

    case MXS_COM_QUERY:
        {
            if (rcap_type_required(m_session->capabilities(), RCAP_TYPE_QUERY_CLASSIFICATION))
            {
                buffer.make_contiguous();
            }

            // Track MaxScale-specific sql. If the variable setting succeeds, the query is routed normally
            // so that the same variable is visible on backend.
            char* errmsg = handle_variables(buffer);
            if (errmsg)
            {
                // No need to route the query, send error to client.
                success = write(modutil_create_mysql_err_msg(1, 0, 1193, "HY000", errmsg)) != 0;
                MXB_FREE(errmsg);
            }
            else
            {
                // Some queries require special handling. Some of these are text versions of other
                // similarly handled commands.
                if (process_special_queries(buffer) == SpecialCmdRes::END)
                {
                    success = true;     // No need to route query.
                }
                else
                {
                    success = route_statement(move(buffer));
                }
            }
        }
        break;

    default:
        // Not a query, just a command which does not require special handling.
        success = route_statement(move(buffer));
        break;
    }

    if (success && is_large)
    {
        // This will fail on non-routed packets. Such packets would be malformed anyway.
        // TODO: Add a DISCARD_LARGE_PACKET state for discarding the tail end of anything we don't support
        if (m_routing_state == RoutingState::RECORD_HISTORY)
        {
            m_routing_state = RoutingState::LARGE_HISTORY_PACKET;
        }
        else
        {
            m_routing_state = RoutingState::LARGE_PACKET;
        }
    }

    return success;
}

void MariaDBClientConnection::write_ok_packet(int sequence, uint8_t affected_rows, const char* message)
{
    write(mxs_mysql_create_ok(sequence, affected_rows, message));
}

bool MariaDBClientConnection::send_mysql_err_packet(int mysql_errno, const char* sqlstate_msg,
                                                    const char* mysql_message)
{
    GWBUF* buf = modutil_create_mysql_err_msg(m_next_sequence, 0, mysql_errno, sqlstate_msg, mysql_message);
    return write(buf);
}

bool
MariaDBClientConnection::clientReply(GWBUF* buffer, maxscale::ReplyRoute& down, const mxs::Reply& reply)
{
    if (m_num_responses == 1)
    {
        switch (m_routing_state)
        {
        case RoutingState::CHANGING_DB:
            if (reply.is_ok())
            {
                // Database change succeeded.
                m_session_data->current_db = move(m_pending_value);
                m_session->notify_userdata_change();
            }
            // Regardless of result, database change is complete.
            m_pending_value.clear();
            m_routing_state = RoutingState::PACKET_START;
            m_dcb->trigger_read_event();
            break;

        case RoutingState::CHANGING_ROLE:
            if (reply.is_ok())
            {
                // Role change succeeded. Role "NONE" is special, in that it means no role is active.
                if (m_pending_value == "NONE")
                {
                    m_session_data->role.clear();
                }
                else
                {
                    m_session_data->role = move(m_pending_value);
                }
                m_session->notify_userdata_change();
            }
            // Regardless of result, role change is complete.
            m_pending_value.clear();
            m_routing_state = RoutingState::PACKET_START;
            m_dcb->trigger_read_event();
            break;

        case RoutingState::CHANGING_USER:
            // Route the reply to client. The sequence in the server packet may be wrong, fix it.
            GWBUF_DATA(buffer)[3] = m_next_sequence;
            if (reply.is_ok())
            {
                complete_change_user_p2();
                m_session->notify_userdata_change();
            }
            else
            {
                // Change user succeeded on MaxScale but failed on backends. Cancel it.
                cancel_change_user_p2(buffer);
            }
            m_routing_state = RoutingState::PACKET_START;
            m_dcb->trigger_read_event();
            break;

        case RoutingState::RECORD_HISTORY:
            finish_recording_history(buffer, reply);
            break;

        default:
            break;
        }
    }

    if (m_command == MXS_COM_BINLOG_DUMP)
    {
        // A COM_BINLOG_DUMP is treated as an endless result. Stop counting the expected responses as the data
        // isn't in the normal result format we expect it to be in. The protocol could go into a more special
        // mode to bypass all processing but this alone cuts out most of it.
    }
    else
    {
        if (reply.is_complete() && !reply.error().is_unexpected_error())
        {
            --m_num_responses;
            mxb_assert(m_num_responses >= 0);

            // TODO: The SERVER parameter should be changed into a mxs::Target
            m_session->book_server_response(static_cast<SERVER*>(down.front()->target()), true);
        }

        if (reply.is_ok() && m_session->service->config()->session_track_trx_state)
        {
            parse_and_set_trx_state(reply);
        }

        if (m_track_pooling_status && !m_pooling_permanent_disable)
        {
            // TODO: Configurable? Also, must be many other situations where backend conns should not be
            // runtime-pooled.
            if (m_session_data->history.size() > m_max_sescmd_history)
            {
                m_pooling_permanent_disable = true;
                m_session->set_can_pool_backends(false);
            }
            else
            {
                bool reply_complete = reply.is_complete();
                bool waiting_response = m_num_responses > 0;
                // Trx status detection is likely lacking.
                bool trx_on = m_session_data->is_trx_active() && !m_session_data->is_trx_ending();
                bool pooling_ok = reply_complete && !waiting_response && !trx_on;
                m_session->set_can_pool_backends(pooling_ok);
            }
        }
    }

    return write(buffer);
}

// Use SESSION_TRACK_STATE_CHANGE, SESSION_TRACK_TRANSACTION_TYPE and
// SESSION_TRACK_TRANSACTION_CHARACTERISTICS to track transaction state.
void MariaDBClientConnection::parse_and_set_trx_state(const mxs::Reply& reply)
{
    auto& ses_trx_state = m_session_data->trx_state;

    // These are defined somewhere in the connector-c headers but including the header directly doesn't work.
    // For the sake of simplicity, just declare them here.
    const uint16_t STATUS_IN_TRX = 1;
    const uint16_t STATUS_AUTOCOMMIT = 2;
    const uint16_t STATUS_IN_RO_TRX = 8192;

    uint16_t status = reply.server_status();
    bool in_trx = status & (STATUS_IN_TRX | STATUS_IN_RO_TRX);
    m_session_data->is_autocommit = status & STATUS_AUTOCOMMIT;
    ses_trx_state = TrxState::TRX_INACTIVE;

    if (!m_session_data->is_autocommit || in_trx)
    {
        ses_trx_state = TrxState::TRX_ACTIVE;

        if (status & STATUS_IN_RO_TRX)
        {
            ses_trx_state |= TrxState::TRX_READ_ONLY;
        }
    }

    auto autocommit = reply.get_variable("autocommit");
    if (!autocommit.empty())
    {
        m_session_data->is_autocommit = strncasecmp(autocommit.c_str(), "ON", 2) == 0;
    }

    auto trx_state = reply.get_variable("trx_state");
    if (!trx_state.empty())
    {
        if (trx_state.find_first_of("TI") != std::string::npos)
        {
            ses_trx_state = TrxState::TRX_ACTIVE;
        }
        else if (trx_state.find_first_of("rRwWsSL") == std::string::npos)
        {
            ses_trx_state = TrxState::TRX_INACTIVE;
        }
    }

    auto trx_characteristics = reply.get_variable("trx_characteristics");
    if (!trx_characteristics.empty())
    {
        if (trx_characteristics == "START TRANSACTION READ ONLY;")
        {
            ses_trx_state = TrxState::TRX_ACTIVE | TrxState::TRX_READ_ONLY;
        }
        else if (trx_characteristics == "START TRANSACTION READ WRITE;")
        {
            ses_trx_state = TrxState::TRX_ACTIVE;
        }
    }
}

void MariaDBClientConnection::add_local_client(LocalClient* client)
{
    // Prune stale LocalClients before adding the new one
    m_local_clients.erase(
        std::remove_if(m_local_clients.begin(), m_local_clients.end(), [](const auto& client) {
        return !client->is_open();
    }), m_local_clients.end());

    m_local_clients.emplace_back(client);
}

void MariaDBClientConnection::kill_complete(bool send_ok, LocalClient* client)
{
    // This needs to be executed once we return from the clientReply or the handleError callback of the
    // LocalClient. In 6.4 this can be changed to use Worker::lcall to make it execute right after the
    // function returns into epoll.
    auto fn = [=]() {
        m_local_clients.erase(
            std::remove_if(m_local_clients.begin(), m_local_clients.end(), [&](const auto& c) {
            return c.get() == client;
        }), m_local_clients.end());
        maybe_send_kill_response(send_ok);
    };

    m_session->worker()->execute(fn, mxb::Worker::EXECUTE_QUEUED);
}

void MariaDBClientConnection::maybe_send_kill_response(bool send_ok)
{
    if (!have_local_clients())
    {
        // Check if the DCB is still open. If MaxScale is shutting down, the DCB is
        // already closed when this callback is called and an error about a write to a
        // closed DCB would be logged.
        if (m_dcb->is_open() && send_ok)
        {
            write_ok_packet(1);
        }

        MXS_INFO("All KILL commands finished");
    }
}

bool MariaDBClientConnection::have_local_clients()
{
    return std::any_of(m_local_clients.begin(), m_local_clients.end(), std::mem_fn(&LocalClient::is_open));
}

void MariaDBClientConnection::kill()
{
    m_local_clients.clear();
}

bool MariaDBClientConnection::module_init()
{
    mxb_assert(this_unit.special_queries_regex.empty());

    /*
     * We need to detect the following queries:
     * 1) USE database
     * 2) SET ROLE { role | NONE }
     * 3) KILL [HARD | SOFT] [CONNECTION | QUERY [ID] ] [thread_id | USER user_name | query_id]
     *
     * Construct one regex which captures all of the above. The "?:" disables capturing for redundant groups.
     * Comments at start are skipped. Executable comments are not parsed.
     */
    const char regex_string[] =
        // <main> captures the entire statement.
        R"((?<main>)"
        // Capture "USE database".
        R"(USE\s+(?<db>\w+))"
        // Capture "SET ROLE role".
        R"(|SET\s+ROLE\s+(?<role>\w+))"
        // Capture KILL ...
        R"(|KILL\s+(?:(?<koption>HARD|SOFT)\s+)?(?:(?<ktype>CONNECTION|QUERY|QUERY\s+ID)\s+)?(?<ktarget>\d+|USER\s+\w+))"
        // End of <main>.
        R"())"
        // Ensure the statement ends nicely. Either subject ends, or a comment begins. This
        // last comment is not properly checked as skipping it is not required.
        R"(\s*(?:;|$|--|#|/\*))";

    bool rval = false;
    mxb::Regex regex(regex_string, PCRE2_CASELESS);
    if (regex.valid())
    {
        this_unit.special_queries_regex = move(regex);
        rval = true;
    }
    else
    {
        MXB_ERROR("Regular expression initialization failed. %s", regex.error().c_str());
    }
    return rval;
}

void MariaDBClientConnection::start_change_role(string&& role)
{
    m_routing_state = RoutingState::CHANGING_ROLE;
    m_pending_value = move(role);
}

void MariaDBClientConnection::start_change_db(string&& db)
{
    m_routing_state = RoutingState::CHANGING_DB;
    m_pending_value = move(db);
}

MariaDBClientConnection::SpecialQueryDesc
MariaDBClientConnection::parse_special_query(const char* sql, int len)
{
    SpecialQueryDesc rval;
    const auto& regex = this_unit.special_queries_regex;
    if (regex.match(sql, len))
    {
        // Is a tracked command. Look at the captured parts to figure out which one it is.
        auto main_ind = regex.substring_ind_by_name("main");
        mxb_assert(!main_ind.empty());
        char c = sql[main_ind.begin];
        switch (c)
        {
        case 'K':
        case 'k':
            {
                rval = parse_kill_query_elems(sql);
            }
            break;

        case 'S':
        case 's':
            {
                rval.type = SpecialQueryDesc::Type::SET_ROLE;
                rval.target = regex.substring_by_name(sql, "role");
            }
            break;

        case 'U':
        case 'u':
            rval.type = SpecialQueryDesc::Type::USE_DB;
            rval.target = regex.substring_by_name(sql, "db");
            break;

        default:
            mxb_assert(!true);
        }
    }
    return rval;
}

void MariaDBClientConnection::assign_backend_authenticator(mariadb::AuthenticationData& auth_data)
{
    // If manual mapping is on, search for the current user or their group. If not found or if not in use,
    // use same authenticator as client.
    const auto* listener_data = m_session->listener_data();
    const auto* mapping_info = listener_data->m_mapping_info.get();
    bool user_is_mapped = false;

    if (mapping_info)
    {
        // Mapping is enabled for the listener. First, search based on username, then based on Linux user
        // group. Mapping does not depend on incoming user IP (can be added later if there is demand).
        const string* mapped_user = nullptr;
        const auto& user = auth_data.user;
        const auto& user_map = mapping_info->user_map;
        auto it_u = user_map.find(user);
        if (it_u != user_map.end())
        {
            mapped_user = &it_u->second;
        }
        else
        {
            // Perhaps the mapping is defined through the user's Linux group.
            const auto& group_map = mapping_info->group_map;
            if (!group_map.empty())
            {
                auto userc = user.c_str();

                // getgrouplist accepts a default group which the user is always a member of. Use user id
                // from passwd-structure.
                gid_t user_group = 0;
                if (call_getpwnam_r(userc, user_group))
                {
                    const int N = 100;      // Check at most 100 groups.
                    gid_t user_gids[N];
                    int n_groups = N;   // Input-output param
                    getgrouplist(userc, user_group, user_gids, &n_groups);
                    int found_groups = std::min(n_groups, N);
                    for (int i = 0; i < found_groups; i++)
                    {
                        // The group id:s of the user's groups are in the array. Go through each, get
                        // text-form group name and compare to mapping. Use first match.
                        string group_name;
                        if (call_getgrgid_r(user_gids[i], group_name))
                        {
                            auto it_g = group_map.find(group_name);
                            if (it_g != group_map.end())
                            {
                                mapped_user = &it_g->second;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (mapped_user)
        {
            // Found a mapped user. Search for credentials. If none found, use defaults.
            const auto& creds = mapping_info->credentials;
            const mxs::ListenerData::UserCreds* found_creds = &default_mapped_creds;
            auto it2 = creds.find(*mapped_user);
            if (it2 != creds.end())
            {
                found_creds = &it2->second;
            }

            // Check that the plugin defined for the user is enabled.
            auto* auth_module = find_auth_module(found_creds->plugin);
            if (auth_module)
            {
                // Found authentication module. Apply mapping.
                auth_data.be_auth_module = auth_module;
                const auto& mapped_pw = found_creds->password;
                MXB_INFO("Incoming user '%s' mapped to '%s' using '%s' with %s.",
                         auth_data.user.c_str(), mapped_user->c_str(), found_creds->plugin.c_str(),
                         mapped_pw.empty() ? "no password" : "password");
                auth_data.user = *mapped_user;      // TODO: save to separate field
                auth_data.backend_token = auth_data.be_auth_module->generate_token(mapped_pw);
                user_is_mapped = true;
            }
            else
            {
                MXB_ERROR("Client %s manually maps to '%s', who uses authenticator plugin '%s'. "
                          "The plugin is not enabled for listener '%s'. Falling back to normal "
                          "authentication.",
                          m_session_data->user_and_host().c_str(), mapped_user->c_str(),
                          mapped_user->c_str(), listener_data->m_listener_name.c_str());
            }
        }
    }

    if (!user_is_mapped)
    {
        // No mapping, use client authenticator.
        auth_data.be_auth_module = auth_data.client_auth_module;
    }
}

mariadb::AuthenticatorModule* MariaDBClientConnection::find_auth_module(const string& plugin_name)
{
    mariadb::AuthenticatorModule* rval = nullptr;
    auto& auth_modules = m_session->listener_data()->m_authenticators;
    for (const auto& auth_module : auth_modules)
    {
        auto protocol_auth = static_cast<mariadb::AuthenticatorModule*>(auth_module.get());
        if (protocol_auth->supported_plugins().count(plugin_name))
        {
            // Found correct authenticator for the user entry.
            rval = protocol_auth;
            break;
        }
    }
    return rval;
}

/**
 * Read protocol packet and update packet sequence.
 */
DCB::ReadResult MariaDBClientConnection::read_protocol_packet()
{
    auto rval = mariadb::read_protocol_packet(m_dcb);
    if (!rval.data.empty())
    {
        uint8_t seq = MYSQL_GET_PACKET_NO(rval.data.data());
        m_sequence = seq;
        m_next_sequence = seq + 1;
    }
    return rval;
}

mariadb::AuthenticationData& MariaDBClientConnection::authentication_data(AuthType type)
{
    return (type == AuthType::NORMAL_AUTH) ? *m_session_data->auth_data : *m_change_user.auth_data;
}
