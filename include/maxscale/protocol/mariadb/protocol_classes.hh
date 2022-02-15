/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>

namespace mariadb
{
using ByteVec = std::vector<uint8_t>;

// Total user search settings structure.
struct UserSearchSettings
{
    // Matches the settings for server variable 'lower_case_table_names'. For authentication purposes, this
    // only changes how database names are handled.
    enum class DBNameCmpMode
    {
        CASE_SENSITIVE,   // Db-name given by client is compared as-is to stored values.
        LOWER_CASE,       // Db-name given by client converted to lowercase. Stored values assumed lowercase.
        CASE_INSENSITIVE  // DB-names are compared case-insensitive.
    };

    struct Listener
    {
        // These user search settings are dependant on listener configuration. Stored in the protocol module.
        bool check_password {true};
        bool match_host_pattern {true};
        bool allow_anon_user {false};

        DBNameCmpMode db_name_cmp_mode {DBNameCmpMode::CASE_SENSITIVE};
    };

    struct Service
    {
        // These user search settings are dependent on service configuration. As services can be reconfigured
        // during runtime, the setting values have to be updated when creating session.
        bool allow_root_user {false};
    };

    Listener listener;
    Service service;
};

/**
 * Contents of an Authentication Switch Request-packet. Defined here for authenticator plugins.
 */
struct AuthSwitchReqContents
{
    bool success {false};    /**< Was parsing successful */
    std::string plugin_name; /**< Plugin name */
    ByteVec plugin_data;     /**< Data for plugin */
};

AuthSwitchReqContents parse_auth_switch_request(const mxs::Buffer& input);
}  // namespace mariadb

/*
 * Data shared with authenticators
 */
class MYSQL_session : public MXS_SESSION::ProtocolData
{
public:
    MYSQL_session() = default;
    MYSQL_session(const MYSQL_session& rhs);

    /**
     * Convenience method to print user and host.
     *
     * @return 'user'@'host'
     */
    std::string user_and_host() const;

    /**
     * Contains client capabilities. The client sends this data in the handshake response-packet, and the
     * same data is sent to backends. Usually only the client protocol should write to these.
     */
    class ClientInfo
    {
    public:
        uint32_t m_client_capabilities {0}; /*< Basic client capabilities */
        uint32_t m_extra_capabilities {0};  /*< MariaDB 10.2 capabilities */

        /** Connection character set. Usually just one byte is needed. COM_CHANGE_USER sends two. */
        uint16_t m_charset {0};
    };

    bool ssl_capable() const;
    uint32_t client_capabilities() const;
    uint32_t extra_capabilitites() const;

    uint8_t scramble[MYSQL_SCRAMBLE_LEN] {0}; /*< Created server scramble */

    std::string user;          /*< username       */
    std::string remote;        /*< client ip      */
    std::string db;            /*< database       */
    std::string plugin;        /*< authentication plugin requested by client */
    uint8_t next_sequence {0}; /*< Next packet sequence */

    // Raw connection attribute data, copied to all backend connections
    std::vector<uint8_t> connect_attrs;

    ClientInfo client_info; /**< Client capabilities from handshake response packet */

    /**
     * Authentication token storage. Used by different authenticators in different ways. So far, only
     * MariaDBAuth uses both tokens, as it needs storage for an intermediary result.
     */
    mariadb::ClientAuthenticator::ByteVec auth_token;
    mariadb::ClientAuthenticator::ByteVec auth_token_phase2;

    // Authenticator module currently in use by the session. May change on COM_CHANGE_USER.
    mariadb::AuthenticatorModule* m_current_authenticator {nullptr};

    // User search settings for the session. Does not change during session lifetime.
    mariadb::UserSearchSettings user_search_settings;

    // User entry used by the session.
    mariadb::UserEntryResult user_entry;
};
