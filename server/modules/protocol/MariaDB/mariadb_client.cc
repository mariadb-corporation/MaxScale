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

std::string get_version_string(SERVICE* service)
{
    std::string rval = DEFAULT_VERSION_STRING;

    if (!service->config().version_string.empty())
    {
        // User-defined version string, use it
        rval = service->config().version_string;
    }
    else
    {
        uint64_t smallest_found = UINT64_MAX;
        for (auto server : service->reachable_servers())
        {
            auto version = server->version();
            if (version.total > 0 && version.total < smallest_found)
            {
                rval = server->version_string();
                smallest_found = version.total;
            }
        }
    }

    // Older applications don't understand versions other than 5 and cause strange problems
    if (rval[0] != '5')
    {
        const char prefix[] = "5.5.5-";
        rval = prefix + rval;
    }

    return rval;
}

uint8_t get_charset(SERVICE* service)
{
    uint8_t rval = 0;

    for (SERVER* s : service->reachable_servers())
    {
        if (s->is_master())
        {
            // Master found, stop searching
            rval = s->charset;
            break;
        }
        else if (s->is_slave() || (s->is_running() && rval == 0))
        {
            // Slaves precede Running servers
            rval = s->charset;
        }
    }

    if (rval == 0)
    {
        // Charset 8 is latin1, the server default
        rval = 8;
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

/**
 * @brief Check whether a DCB requires SSL.
 *
 * This is a very simple test, but is placed in an SSL function so that
 * the knowledge of the SSL process is removed from the more general
 * handling of a connection in the protocols.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether SSL is required.
 */
bool ssl_required_by_dcb(DCB* dcb)
{
    mxb_assert(dcb->session()->listener);
    return dcb->session()->listener->ssl().context();
}

/**
 * @brief Check whether a DCB requires SSL, but SSL is not yet negotiated.
 *
 * This is a very simple test, but is placed in an SSL function so that
 * the knowledge of the SSL process is removed from the more general
 * handling of a connection in the protocols.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether SSL is required and not negotiated.
 */
bool ssl_required_but_not_negotiated(DCB* dcb)
{
    return ssl_required_by_dcb(dcb) && DCB::SSLState::HANDSHAKE_UNKNOWN == dcb->ssl_state();
}

/**
 * Get length of a null-terminated string
 *
 * @param str String to measure
 * @param len Maximum length to read
 *
 * @return Length of @c str or -1 if the string is not null-terminated
 */
int get_zstr_len(const char* str, int len)
{
    const char* end = str + len;
    int slen = 0;

    while (str < end && *str)
    {
        str++;
        slen++;
    }

    if (str == end)
    {
        // The string is not null terminated
        slen = -1;
    }

    return slen;
}

/**
 * @brief Debug check function for authentication packets
 *
 * Check that the packet is consistent with how the protocol works and that no
 * unexpected data is processed.
 *
 * @param dcb Client DCB
 * @param buf Buffer containing packet
 * @param bytes Number of bytes available
 */
void check_packet(DCB* dcb, GWBUF* buf, int bytes)
{
    uint8_t hdr[MYSQL_HEADER_LEN];
    mxb_assert(gwbuf_copy_data(buf, 0, MYSQL_HEADER_LEN, hdr) == MYSQL_HEADER_LEN);

    int buflen = gwbuf_length(buf);
    int pktlen = MYSQL_GET_PAYLOAD_LEN(hdr) + MYSQL_HEADER_LEN;

    if (bytes == MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        /** This is an SSL request packet */
        mxb_assert(dcb->session()->listener->ssl().context());
        mxb_assert(buflen == bytes && pktlen >= buflen);
    }
    else
    {
        /** Normal packet */
        mxb_assert(buflen == pktlen);
    }
}

/**
 * @brief If an SSL connection is required, check that it has been established.
 *
 * This is called at the end of the authentication of a new connection.
 * If the result is not true, the data packet is abandoned with further
 * data expected from the client.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean to indicate whether connection is healthy
 */
bool ssl_is_connection_healthy(DCB* dcb)
{
    /**
     * If SSL was never expected, or if the connection has state SSL_ESTABLISHED
     * then everything is as we wish. Otherwise, either there is a problem or
     * more to be done.
     */
    return !dcb->session()->listener->ssl().context() || dcb->ssl_state() == DCB::SSLState::ESTABLISHED;
}

/* Looks to be redundant - can remove include for ioctl too */
bool ssl_check_data_to_process(DCB* dcb)
{
    /** SSL authentication is still going on, we need to call DCB::ssl_handshake
     * until it return 1 for success or -1 for error */
    if (dcb->ssl_state() == DCB::SSLState::HANDSHAKE_REQUIRED && 1 == dcb->ssl_handshake())
    {
        int b = 0;
        ioctl(dcb->fd(), FIONREAD, &b);
        if (b != 0)
        {
            return true;
        }
        else
        {
            MXS_DEBUG("[mariadbclient_read] No data in socket after SSL auth");
        }
    }
    return false;
}

/**
 * @brief Check client's SSL capability and start SSL if appropriate.
 *
 * The protocol should determine whether the client is SSL capable and pass
 * the result as the second parameter. If the listener requires SSL but the
 * client is not SSL capable, an error message is recorded and failure return
 * given. If both sides want SSL, and SSL is not already established, the
 * process is triggered by calling DCB::ssl_handshake.
 *
 * @param dcb Request handler DCB connected to the client
 * @param is_capable Indicates if the client can handle SSL
 * @return 0 if ok, >0 if a problem - see return codes defined in ssl.h
 */
int ssl_authenticate_client(DCB* dcb, bool is_capable)
{
    const std::string& user = dcb->session()->user();
    const std::string& remote = dcb->remote();
    const char* service = (dcb->service() && dcb->service()->name()) ? dcb->service()->name() : "";

    if (!dcb->session()->listener->ssl().context())
    {
        /* Not an SSL connection on account of listener configuration */
        return SSL_AUTH_CHECKS_OK;
    }
    /* Now we require an SSL connection */
    if (!is_capable)
    {
        /* Should be SSL, but client is not SSL capable */
        MXS_INFO("User %s@%s connected to service '%s' without SSL when SSL was required.",
                 user.c_str(),
                 remote.c_str(),
                 service);
        return SSL_ERROR_CLIENT_NOT_SSL;
    }
    /* Now we know SSL is required and client is capable */
    if (dcb->ssl_state() != DCB::SSLState::HANDSHAKE_DONE && dcb->ssl_state() != DCB::SSLState::ESTABLISHED)
    {
        int return_code;
        /** Do the SSL Handshake */
        if (DCB::SSLState::HANDSHAKE_UNKNOWN == dcb->ssl_state())
        {
            dcb->set_ssl_state(DCB::SSLState::HANDSHAKE_REQUIRED);
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
            MXS_INFO("User %s@%s failed to connect to service '%s' with SSL.",
                     user.c_str(),
                     remote.c_str(),
                     service);
            return SSL_ERROR_ACCEPT_FAILED;
        }
        else if (mxs_log_is_priority_enabled(LOG_INFO))
        {
            if (1 == return_code)
            {
                MXS_INFO("User %s@%s connected to service '%s' with SSL.",
                         user.c_str(),
                         remote.c_str(),
                         service);
            }
            else
            {
                MXS_INFO("User %s@%s connect to service '%s' with SSL in progress.",
                         user.c_str(),
                         remote.c_str(),
                         service);
            }
        }
    }
    return SSL_AUTH_CHECKS_OK;
}

void extract_user(char* token, std::string* user)
{
    char* end = strchr(token, ';');

    if (end)
    {
        user->assign(token, end - token);
    }
    else
    {
        user->assign(token);
    }
}

bool is_use_database(GWBUF* buffer, size_t packet_len)
{
    const char USE[] = "USE ";
    char* ptr = (char*)GWBUF_DATA(buffer) + MYSQL_HEADER_LEN + 1;

    return packet_len > MYSQL_HEADER_LEN + 1 + (sizeof(USE) - 1)
           && strncasecmp(ptr, USE, sizeof(USE) - 1) == 0;
}

bool is_kill_query(GWBUF* buffer, size_t packet_len)
{
    const char KILL[] = "KILL ";
    char* ptr = (char*)GWBUF_DATA(buffer) + MYSQL_HEADER_LEN + 1;

    return packet_len > MYSQL_HEADER_LEN + 1 + (sizeof(KILL) - 1)
           && strncasecmp(ptr, KILL, sizeof(KILL) - 1) == 0;
}
}

MariaDBClientConnection::SSLState MariaDBClientConnection::ssl_authenticate_check_status()
{
    auto rval = SSLState::FAIL;

    /**
     * We record the SSL status before and after ssl authentication. This allows
     * us to detect if the SSL handshake is immediately completed, which means more
     * data needs to be read from the socket.
     */
    bool health_before = ssl_is_connection_healthy(m_dcb);
    int ssl_ret = ssl_authenticate_client(m_dcb, m_session_data->ssl_capable());
    bool health_after = ssl_is_connection_healthy(m_dcb);

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
    uint8_t mysql_server_language = get_charset(service);
    uint8_t mysql_server_status[2];
    uint8_t mysql_scramble_len = 21;
    uint8_t mysql_filler_ten[10] = {};
    /* uint8_t mysql_last_byte = 0x00; not needed */
    char server_scramble[GW_MYSQL_SCRAMBLE_SIZE + 1] = "";
    bool is_maria = supports_extended_caps(service);

    std::string version = get_version_string(service);

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

    if (ssl_required_by_dcb(m_dcb))
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

    // Check that we match the old values
    mxb_assert(mysql_server_capabilities_two[0] == 15);
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
 * @brief Store client connection information into the session
 *
 * @param buffer Buffer containing the handshake response packet
 */
void MariaDBClientConnection::store_client_information(GWBUF* buffer)
{
    size_t len = gwbuf_length(buffer);
    uint8_t data[len];
    MYSQL_session* ses = m_session_data;

    gwbuf_copy_data(buffer, 0, len, data);
    mxb_assert(MYSQL_GET_PAYLOAD_LEN(data) + MYSQL_HEADER_LEN == len
               || len == MYSQL_AUTH_PACKET_BASE_SIZE);      // For SSL request packet

    // We OR the capability bits in order to retain the starting bits sent
    // when an SSL connection is opened. Oracle Connector/J 8.0 appears to drop
    // the SSL capability bit mid-authentication which causes MaxScale to think
    // that SSL is not used.
    ses->client_info.m_client_capabilities |= gw_mysql_get_byte4(data + MYSQL_CLIENT_CAP_OFFSET);
    ses->client_info.m_charset = data[MYSQL_CHARSET_OFFSET];

    /** MariaDB 10.2 compatible clients don't set the first bit to signal that
     * there are extra capabilities stored in the last 4 bytes of the 23 byte filler. */
    if ((ses->client_info.m_client_capabilities & GW_MYSQL_CAPABILITIES_CLIENT_MYSQL) == 0)
    {
        ses->client_info.m_extra_capabilities = gw_mysql_get_byte4(data + MARIADB_CAP_OFFSET);
    }

    if (len > MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        const char* username = (const char*)data + MYSQL_AUTH_PACKET_BASE_SIZE;
        int userlen = get_zstr_len(username, len - MYSQL_AUTH_PACKET_BASE_SIZE);

        if (userlen != -1)
        {
            ses->user = username;

            // Include the null terminator in the user length
            userlen++;

            if (ses->client_info.m_client_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB)
            {
                /** Client is connecting with a default database */
                uint8_t authlen = data[MYSQL_AUTH_PACKET_BASE_SIZE + userlen];
                size_t dboffset = MYSQL_AUTH_PACKET_BASE_SIZE + userlen + authlen + 1;

                if (dboffset < len)
                {
                    int dblen = get_zstr_len((const char*)data + dboffset, len - dboffset);

                    if (dblen != -1)
                    {
                        ses->db = (const char*)data + dboffset;
                    }
                }
            }
        }
    }
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
 * @brief Client read event, process when client not yet authenticated
 *
 * @param buffer A buffer containing the data read from client
 * @return 0 if succeed, 1 otherwise
 */
int MariaDBClientConnection::perform_authentication(GWBUF* buffer)
{
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

    auto remote = m_dcb->remote().c_str();
    bool error = false;
    bool state_machine_continue = true;
    while (state_machine_continue)
    {
        switch (m_auth_state)
        {
        case AuthState::INIT:
            m_auth_state = ssl_required_by_dcb(m_dcb) ? AuthState::EXPECT_SSL_REQ :
                AuthState::EXPECT_HS_RESP;
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
                int expected_seq = ssl_required_by_dcb(m_dcb) ? 2 : 1;
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
                    // Authentication failed. TODO: send an error message here when needed.
                    m_auth_state = AuthState::FAIL;
                    mxb_assert(m_session->listener);
                    m_session->listener->mark_auth_as_failed(m_dcb->remote());
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
                        mysql_send_auth_error(m_dcb, next_seq, "Access denied, wrong password");
                    }
                    else
                    {
                        mysql_send_auth_error(m_dcb, next_seq, "Access denied");
                    }
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
            m_sql_mode = m_session->listener->sql_mode();
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
            DCB::close(m_dcb);
            state_machine_continue = false;
            error = true;
            break;
        }
    }

    /* One way or another, the buffer is now fully processed */
    gwbuf_free(buffer);
    return error ? 1 : 0;
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
MariaDBClientConnection::spec_com_res_t
MariaDBClientConnection::handle_query_kill(DCB* dcb, GWBUF* read_buffer, uint32_t packet_len)
{
    spec_com_res_t rval = RES_CONTINUE;
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
            rval = RES_END;

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
MariaDBClientConnection::spec_com_res_t
MariaDBClientConnection::process_special_commands(DCB* dcb, GWBUF* read_buffer, uint8_t cmd)
{
    spec_com_res_t rval = RES_CONTINUE;

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
        rval = RES_END;
    }
    else if (m_command == MXS_COM_INIT_DB)
    {
        char* start = (char*)GWBUF_DATA(read_buffer);
        char* end = start + GWBUF_LENGTH(read_buffer);
        start += MYSQL_HEADER_LEN + 1;
        m_session_data->db.assign(start, end);
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

        if (process_special_commands(m_dcb, packetbuf, m_command) == RES_END)
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
 * @param read_buffer   A buffer containing the data read from client
 * @param nbytes_read   The number of bytes of data read
 * @return 0 if succeed, 1 otherwise
 */
int MariaDBClientConnection::perform_normal_read(GWBUF* read_buffer, uint32_t nbytes_read)
{
    auto session_state_value = m_session->state();
    if (session_state_value != MXS_SESSION::State::STARTED)
    {
        if (session_state_value != MXS_SESSION::State::STOPPING)
        {
            MXS_ERROR("Session received a query in incorrect state: %s",
                      session_state_to_string(session_state_value));
        }
        gwbuf_free(read_buffer);
        DCB::close(m_dcb);
        return 1;
    }

    // Make sure that a complete packet is read before continuing
    uint8_t pktlen[MYSQL_HEADER_LEN];
    size_t n_copied = gwbuf_copy_data(read_buffer, 0, MYSQL_HEADER_LEN, pktlen);

    if (n_copied != sizeof(pktlen) || nbytes_read < MYSQL_GET_PAYLOAD_LEN(pktlen) + MYSQL_HEADER_LEN)
    {
        m_dcb->readq_append(read_buffer);
        return 0;
    }

    // The query classifier classifies according to the service's server that has the smallest version number
    qc_set_server_version(m_version);

    /**
     * Feed each statement completely and separately to router.
     */
    auto capabilities = service_get_capabilities(m_session->service);
    int rval = route_by_statement(capabilities, &read_buffer) ? 0 : 1;

    if (read_buffer != NULL)
    {
        // Must have been data left over, add incomplete mysql packet to read queue
        m_dcb->readq_append(read_buffer);
    }

    if (rval != 0)
    {
        /** Routing failed, close the client connection */
        m_session->close_reason = SESSION_CLOSE_ROUTING_FAILED;
        DCB::close(m_dcb);
        MXS_ERROR("Routing the query failed. Session will be closed.");
    }
    else if (m_command == MXS_COM_QUIT)
    {
        /** Close router session which causes closing of backends */
        mxb_assert_message(session_valid_for_pool(m_session), "Session should qualify for pooling");
        DCB::close(m_dcb);
    }

    return rval;
}

/**
 * MXS_PROTOCOL_API implementation.
 */

void MariaDBClientConnection::ready_for_reading(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);     // The protocol should only handle its own events.

    /**
     * The use of max_bytes seems like a hack, but no better option is available
     * at the time of writing. When a MySQL server receives a new connection
     * request, it sends an Initial Handshake Packet. Where the client wants to
     * use SSL, it responds with an SSL Request Packet (in place of a Handshake
     * Response Packet). The SSL Request Packet contains only the basic header,
     * and not the user credentials. It is 36 bytes long.  The server then
     * initiates the SSL handshake (via calls to OpenSSL).
     *
     * In many cases, this is what happens. But occasionally, the client seems
     * to send a packet much larger than 36 bytes (in tests it was 333 bytes).
     * If the whole of the packet is read, it is then lost to the SSL handshake
     * process. Why this happens is presently unknown. Reading just 36 bytes
     * when the server requires SSL and SSL has not yet been negotiated seems
     * to solve the problem.
     *
     * If a neater solution can be found, so much the better.
     */
    uint32_t max_bytes = 0;
    if (ssl_required_but_not_negotiated(m_dcb))
    {
        max_bytes = 36;
    }

    const uint32_t max_single_read = GW_MYSQL_MAX_PACKET_LEN + MYSQL_HEADER_LEN;
    GWBUF* read_buffer = nullptr;

    int return_code = m_dcb->read(&read_buffer, max_bytes > 0 ? max_bytes : max_single_read);
    if (return_code < 0)
    {
        DCB::close(m_dcb);
    }

    uint32_t nbytes_read = read_buffer ? gwbuf_length(read_buffer) : 0;
    if (nbytes_read == 0)
    {
        return;
    }

    if (nbytes_read == max_single_read && m_dcb->socket_bytes_readable() > 0)
    {
        // We read a maximally long packet, route it first. This is done in case there's a lot more data
        // waiting and we have to start throttling the reads.
        m_dcb->trigger_read_event();
    }

    return_code = 0;

    switch (m_state)
    {
    /**
     *
     * When a listener receives a new connection request, it creates a
     * request handler DCB to for the client connection. The listener also
     * sends the initial authentication request to the client. The first
     * time this function is called from the poll loop, the client reply
     * to the authentication request should be available.
     *
     * If the authentication is successful the protocol authentication state
     * will be changed to MYSQL_IDLE (see below).
     *
     */
    case State::AUTHENTICATING:
        if (nbytes_read < 3
            || (0 == max_bytes && nbytes_read < MYSQL_GET_PACKET_LEN(read_buffer))
            || (0 != max_bytes && nbytes_read < max_bytes))
        {
            m_dcb->readq_append(read_buffer);
        }
        else
        {
            if (nbytes_read > MYSQL_GET_PACKET_LEN(read_buffer))
            {
                // We read more data than was needed
                m_dcb->readq_append(read_buffer);
                GWBUF* readq = m_dcb->readq_release();
                read_buffer = modutil_get_next_MySQL_packet(&readq);
                m_dcb->readq_set(readq);
            }

            return_code = perform_authentication(read_buffer);
        }
        break;

    // Client connection is authenticated.
    case State::READY:
        /* After this call read_buffer will point to freed data */
        return_code = perform_normal_read(read_buffer, nbytes_read);
        break;

    case State::FAILED:
        gwbuf_free(read_buffer);
        return_code = 1;
        break;

    default:
        MXS_ERROR("In mysql_client.c unexpected protocol authentication state");
        break;
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

std::string MariaDBClientConnection::to_string(AuthState state)
{
    switch (state)
    {
    case AuthState::INIT:
        return "Authentication initialized";

    case AuthState::FAIL:
        return "Authentication failed";

    case AuthState::COMPLETE:
        return "Authentication is complete.";

    default:
        return "MySQL (unknown protocol state)";
    }
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
    if (len == MYSQL_HEADER_LEN + CLIENT_CAPABILITIES_LEN)
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
    size_t min_expected_len = MYSQL_HEADER_LEN + CLIENT_CAPABILITIES_LEN + 2;
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
            }

            if (plugin)
            {
                m_session_data->plugin = plugin;
            }
        }
    }
    return !error;
}
