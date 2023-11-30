/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/history.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/queryclassifier.hh>

#include <deque>

namespace mariadb
{

// Total user search settings structure.
struct UserSearchSettings
{
    // Matches the settings for server variable 'lower_case_table_names'. For authentication purposes, this
    // only changes how database names are handled.
    enum class DBNameCmpMode
    {
        CASE_SENSITIVE, // Db-name given by client is compared as-is to stored values.
        LOWER_CASE,     // Db-name given by client converted to lowercase. Stored values assumed lowercase.
        CASE_INSENSITIVE// DB-names are compared case-insensitive.
    };

    struct Listener
    {
        // These user search settings are dependent on listener configuration. Stored in the protocol module.
        bool check_password {true};
        bool match_host_pattern {true};
        bool allow_anon_user {false};
        bool passthrough_auth {false};

        DBNameCmpMode db_name_cmp_mode {DBNameCmpMode::CASE_SENSITIVE};
    };

    struct Service
    {
        // These user search settings are dependent on service configuration. As services can be reconfigured
        // during runtime, the setting values have to be updated when creating session.
        bool allow_root_user {false};
    };

    Listener listener;
    Service  service;
};

/**
 * Contents of an Authentication Switch Request-packet. Defined here for authenticator plugins.
 */
struct AuthSwitchReqContents
{
    bool        success {false};/**< Was parsing successful */
    std::string plugin_name;    /**< Plugin name */
    ByteVec     plugin_data;    /**< Data for plugin */
};

AuthSwitchReqContents   parse_auth_switch_request(const GWBUF& input);
std::tuple<bool, GWBUF> read_protocol_packet(DCB* dcb);
}

/*
 * Protocol-specific session data
 */
class MYSQL_session : public mxs::ProtocolData
{
public:
    MYSQL_session(size_t limit, bool allow_pruning, bool disable_history)
        : m_history(limit, allow_pruning, disable_history)
    {
    }

    MYSQL_session(const MYSQL_session& rhs) = delete;

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
    struct ClientCapabilities
    {
        uint32_t basic_capabilities {0};        /*< Basic client capabilities */
        uint32_t ext_capabilities {0};          /*< MariaDB 10.2 capabilities (extended capabilities) */
        uint64_t advertised_capabilities {0};   /*< The capabilities that were sent in the handshake packet */
    };

    bool ssl_capable() const;

    uint32_t client_capabilities() const
    {
        return client_caps.basic_capabilities;
    }

    uint32_t extra_capabilities() const
    {
        return client_caps.ext_capabilities;
    }

    uint64_t full_capabilities() const
    {
        return client_capabilities() | (uint64_t)extra_capabilities() << 32;
    }


    uint8_t scramble[MYSQL_SCRAMBLE_LEN] {0};   /*< Created server scramble */

    std::string remote;         /**< client ip */
    /** Resolved hostname. Empty if rDNS not ran. Empty string if rDNS failed */
    std::optional<std::string> host;

    std::string current_db;     /**< Current default database */
    std::string role;           /**< Current role */

    mariadb::SAuthData auth_data;   /**< Authentication data used by backends */
    ClientCapabilities client_caps; /**< Client capabilities from handshake response packet */

    bool client_conn_encrypted {false};     /**< Is connection to client using SSL? */

    /**< Backend authentication passthrough callback. */
    std::function<void(GWBUF && auth_result)> passthrough_be_auth_cb;

    // User search settings for the session. Does not change during session lifetime.
    mariadb::UserSearchSettings user_search_settings;

    // Metadata for COM_STMT_EXECUTE
    std::map<uint32_t, std::vector<uint8_t>> exec_metadata;

    mxs::History& history()
    {
        return m_history;
    }

    const mxs::History& history() const
    {
        return m_history;
    }

    bool will_respond(const GWBUF& buffer) const override;

    bool can_recover_state() const override;

    /**
     * Tells whether a transaction is starting.
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if a new transaction is currently starting
     */
    bool is_trx_starting() const override
    {
        return m_trx_tracker.is_trx_starting();
    }

    /**
     * Tells whether a transaction is active.
     *
     * @see get_trx_state
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if a transaction is active, false otherwise.
     */
    bool is_trx_active() const override
    {
        return m_trx_tracker.is_trx_active();
    }

    /**
     * Tells whether an explicit READ ONLY transaction is active.
     *
     * @see get_trx_state
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if an explicit READ ONLY transaction is active,
     *         false otherwise.
     */
    bool is_trx_read_only() const override
    {
        return m_trx_tracker.is_trx_read_only();
    }

    /**
     * Tells whether a transaction is ending.
     *
     * @see get_trx_state
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if a transaction that was active is ending either via COMMIT or ROLLBACK.
     */
    bool is_trx_ending() const override
    {
        return m_trx_tracker.is_trx_ending();
    }

    bool is_autocommit() const override
    {
        return m_trx_tracker.is_autocommit();
    }

    void set_autocommit(bool value)
    {
        m_trx_tracker.set_autocommit(value);
    }

    bool are_multi_statements_allowed() const override;

    size_t amend_memory_statistics(json_t* memory) const override final;

    size_t static_size() const override final;

    size_t varying_size() const override final;

    /**
     * Sets the capabilities required by the client protocol, to be used by the
     * backend protocol. This is primarily intended for client protocols other than
     * MariaDB that use the MariaDB backend protocol.
     *
     * @param capabilities  The needed capabilities; a bitmask of @c mxs_routing_capability_t
     *                      values. Only output capabilities will have an effect.
     */
    void set_client_protocol_capabilities(uint64_t capabilities)
    {
        m_client_protocol_capabilities |= capabilities;
    }

    /**
     * @returns  The client protocol capabilities; a bitmask of @c mxs_routing_capability values.
     */
    uint64_t client_protocol_capabilities() const
    {
        return m_client_protocol_capabilities;
    }

    mariadb::TrxTracker& trx_tracker()
    {
        return m_trx_tracker;
    }

private:
    size_t get_size(size_t* sescmd_history_size, size_t* exec_metadata_size) const;

    uint64_t m_client_protocol_capabilities {0};

    // The session command history
    mxs::History m_history;

    // Transaction state tracker
    mariadb::TrxTracker m_trx_tracker;
};
