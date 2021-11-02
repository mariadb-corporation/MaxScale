/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>

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

AuthSwitchReqContents parse_auth_switch_request(const mxs::Buffer& input);
DCB::ReadResult       read_protocol_packet(DCB* dcb);
}

/*
 * Protocol-specific session data
 */
class MYSQL_session : public MXS_SESSION::ProtocolData
{
public:
    MYSQL_session() = default;
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
        uint32_t basic_capabilities {0};    /*< Basic client capabilities */
        uint32_t ext_capabilities {0};      /*< MariaDB 10.2 capabilities (extended capabilities) */
    };

    // The struct used to communicate information from the backend protocol to the client protocol.
    struct HistoryInfo
    {
        // Callback to call when the current command being recorded completes. Used to verify the responses
        // from backends that arrived before the accepted answer arrived.
        std::function<void ()> response_cb;

        // Current position in history. Used to track the responses that are still needed.
        uint32_t position {0};
    };

    bool     ssl_capable() const;
    uint32_t client_capabilities() const;
    uint32_t extra_capabilitites() const;

    uint8_t scramble[MYSQL_SCRAMBLE_LEN] {0};   /*< Created server scramble */

    std::string remote;         /**< client ip */
    std::string current_db;     /**< Current default database */
    std::string role;           /**< Current role */

    mariadb::SAuthData auth_data;   /**< Authentication data used by backends */
    ClientCapabilities client_caps; /**< Client capabilities from handshake response packet */

    // User search settings for the session. Does not change during session lifetime.
    mariadb::UserSearchSettings user_search_settings;

    // History of all commands that modify the session state
    std::deque<mxs::Buffer> history;

    // The responses to the executed commands, contains the ID and the result
    std::map<uint32_t, bool> history_responses;

    // Metadata for COM_STMT_EXECUTE
    std::map<uint32_t, std::vector<uint8_t>> exec_metadata;

    // Whether the history has been pruned of old commands. If true, reconnection should only take place if it
    // is acceptable to lose some state history (i.e. prune_sescmd_history is enabled).
    bool history_pruned {false};

    // History information for all open backend connections
    std::unordered_map<mxs::BackendConnection*, HistoryInfo> history_info;

    /**
     * Tells whether autocommit is ON or not. The value effectively only tells the last value
     * of the statement "set autocommit=...".
     *
     * That is, if the statement "set autocommit=1" has been executed, then even if a transaction has
     * been started, which implicitly will cause autocommit to be set to 0 for the duration of the
     * transaction, this value will be true.
     *
     * By default autocommit is ON. Only the client protocol connection should modify this.
     *
     * @see get_trx_state
     */
    bool is_autocommit {false};

    enum TrxState : uint32_t
    {
        TRX_INACTIVE  = 0,
        TRX_ACTIVE    = 1 << 0,
        TRX_READ_ONLY = 1 << 1,
        TRX_ENDING    = 1 << 2,
        TRX_STARTING  = 1 << 3,
    };

    /**
     * The transaction state of the session.
     *
     * This tells only the state of @e explicitly started transactions.
     * That is, if @e autocommit is OFF, which means that there is always an
     * active transaction that is ended with an explicit COMMIT or ROLLBACK,
     * at which point a new transaction is started, this variable will still
     * be TRX_INACTIVE, unless a transaction has explicitly been
     * started with START TRANSACTION.
     *
     * Likewise, if @e autocommit is ON, which means that every statement is
     * executed in a transaction of its own, this will return false, unless a
     * transaction has explicitly been started with START TRANSACTION.
     *
     * The value is valid only if either a router or a filter
     * has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * Only the client protocol object should write this.
     */
    uint32_t trx_state {TRX_INACTIVE};

    /**
     * Tells whether a transaction is starting.
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if a new transaction is currently starting
     */
    bool is_trx_starting() const override;

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
    bool is_trx_active() const override;

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
    bool is_trx_read_only() const override;

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
    bool is_trx_ending() const override;

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

private:
    uint64_t m_client_protocol_capabilities {0};
};
