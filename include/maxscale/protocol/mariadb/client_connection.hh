/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/local_client.hh>
#include <maxscale/protocol/mariadb/queryclassifier.hh>

struct KillInfo;

class MariaDBUserManager;
class MariaDBUserCache;

class MariaDBClientConnection : public mxs::ClientConnectionBase
                              , public mariadb::QueryClassifier::Handler
{
public:
    /* Type of the kill-command sent by client. */
    enum kill_type_t : uint32_t
    {
        KT_SOFT       = (1 << 0),
        KT_HARD       = (1 << 1),
        KT_CONNECTION = (1 << 2),
        KT_QUERY      = (1 << 3),
        KT_QUERY_ID   = (1 << 4),
    };

    MariaDBClientConnection(MXS_SESSION* session, mxs::Component* component);

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(GWBUF* buffer) override;

    bool    init_connection() override;
    void    finish_connection() override;
    int32_t connlimit(int limit) override;
    void    wakeup() override;
    bool    is_movable() const override;
    bool    is_idle() const override;
    void    kill() override;

    std::string current_db() const override;

    struct SpecialQueryDesc
    {
        enum class Type {NONE, KILL, SET_ROLE, USE_DB};
        Type type {Type::NONE};         /**< Query type */

        std::string target;             /**< Db or role to change to, or target user for kill */
        uint32_t    kill_options {0};   /**< Kill options bitfield */
        uint64_t    kill_id {0};        /**< Thread or query id for kill */
    };

    /**
     * Parse elements from a tracked query. This function reads values from thread-local regular expression
     * match data, so it should be called right after a regex has matched.
     *
     * @param sql Original query
     * @return Query fields
     */
    static SpecialQueryDesc parse_special_query(const char* sql, int len);

    void mxs_mysql_execute_kill(uint64_t target_id, kill_type_t type);
    bool in_routing_state() const override;

    json_t* diagnostics() const override;

    bool clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    /**
     * Initialize module-level globals.
     *
     * @return True on success
     */
    static bool module_init();

    //
    // API functions for mariadb::QueryClassifier::Handler
    //

    bool lock_to_master() override
    {
        return true;
    }

    bool is_locked_to_master() const override
    {
        return false;
    }

    bool supports_hint(HINT_TYPE hint_type) const override
    {
        return false;
    }

private:
    /** Return type of process_special_commands() */
    enum class SpecialCmdRes
    {
        CONTINUE,   // No special command detected, proceed as normal.
        END,        // Query handling completed, do not send to filters/router.
    };

    /** Return type of a lower level state machine */
    enum class StateMachineRes
    {
        IN_PROGRESS,// The SM should be called again once more data is available.
        DONE,       // The SM is complete for now, the protocol may advance to next state
        ERROR,      // The SM encountered an error. The connection should be closed.
    };

    enum class AuthType
    {
        NORMAL_AUTH,
        CHANGE_USER,
    };

    bool            read_first_client_packet(mxs::Buffer* output);
    DCB::ReadResult read_protocol_packet();

    StateMachineRes process_handshake();
    StateMachineRes process_authentication(AuthType auth_type);
    StateMachineRes process_normal_read();

    bool send_server_handshake();
    bool parse_ssl_request_packet(GWBUF* buffer);
    bool parse_handshake_response_packet(GWBUF* buffer);

    bool perform_auth_exchange(mariadb::AuthenticationData& auth_data);
    void perform_check_token(AuthType auth_type);
    bool process_normal_packet(mxs::Buffer&& buffer);
    bool route_statement(mxs::Buffer&& buffer);
    void finish_recording_history(const GWBUF* buffer, const mxs::Reply& reply);
    bool record_for_history(mxs::Buffer& buffer, uint8_t cmd);
    void prune_history();

    bool start_change_user(mxs::Buffer&& buffer);
    bool complete_change_user_p1();
    void complete_change_user_p2();
    void cancel_change_user_p1();
    void cancel_change_user_p2();

    void  handle_use_database(GWBUF* read_buffer);
    char* handle_variables(mxs::Buffer& buffer);

    bool          should_inspect_query(mxs::Buffer& buffer) const;
    SpecialCmdRes process_special_queries(mxs::Buffer& buffer);
    void          handle_query_kill(const SpecialQueryDesc& kill_contents);

    void add_local_client(LocalClient* client);

    void execute_kill_all_others(uint64_t target_id, uint64_t keep_protocol_thread_id, kill_type_t type);
    void execute_kill_user(const char* user, kill_type_t type);
    void execute_kill(std::shared_ptr<KillInfo> info);

    void track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf);
    void track_current_command(const mxs::Buffer& buf);
    bool large_query_continues(const mxs::Buffer& buffer) const;
    bool require_ssl() const;

    void update_user_account_entry(mariadb::AuthenticationData& auth_data);
    void assign_backend_authenticator(mariadb::AuthenticationData& auth_data);

    mariadb::AuthenticatorModule* find_auth_module(const std::string& plugin_name);
    const MariaDBUserCache*       user_account_cache();
    mariadb::AuthenticationData&  authentication_data(AuthType auth_type);

    enum class AuthErrorType
    {
        ACCESS_DENIED,
        DB_ACCESS_DENIED,
        BAD_DB,
        NO_PLUGIN,
    };

    void   send_authentication_error(AuthErrorType error, const std::string& auth_mod_msg = "");
    void   send_misc_error(const std::string& msg);
    int    send_auth_error(int packet_number, const char* mysql_message);
    int    send_standard_error(int packet_number, int error_number, const char* error_message);
    GWBUF* create_standard_error(int sequence, int error_number, const char* msg);
    void   write_ok_packet(int sequence, uint8_t affected_rows = 0, const char* message = nullptr);

    /**
     * Send an error packet to the client.
     *
     * @param mysql_errno Error number
     * @param sqlstate_msg MySQL state message
     * @param mysql_message Error message
     * @return True on success
     */
    bool send_mysql_err_packet(int mysql_errno, const char* sqlstate_msg, const char* mysql_message);

    void parse_and_set_trx_state(const mxs::Reply& reply);
    void start_change_role(std::string&& role);
    void start_change_db(std::string&& db);

    static SpecialQueryDesc parse_kill_query_elems(const char* sql);

    // General connection state
    enum class State
    {
        HANDSHAKING,
        AUTHENTICATING,
        CHANGING_USER,
        READY,
        FAILED,
        QUIT,
    };

    // Handshake state
    enum class HSState
    {
        INIT,           /**< Initial handshake state */
        EXPECT_SSL_REQ, /**< Expecting client to send SSLRequest */
        SSL_NEG,        /**< Negotiate SSL*/
        EXPECT_HS_RESP, /**< Expecting client to send standard handshake response */
        COMPLETE,       /**< Handshake succeeded */
        FAIL,           /**< Handshake failed */
    };

    // Authentication state
    enum class AuthState
    {
        FIND_ENTRY,         /**< Find user account entry */
        TRY_AGAIN,          /**< Find user entry again with new data */
        NO_PLUGIN,          /**< Requested plugin is not loaded */
        START_EXCHANGE,     /**< Begin authenticator module exchange */
        CONTINUE_EXCHANGE,  /**< Continue exchange */
        CHECK_TOKEN,        /**< Check token against user account entry */
        START_SESSION,      /**< Start routing session */
        CHANGE_USER_OK,     /**< User-change processed */
        FAIL,               /**< Authentication failed */
        COMPLETE,           /**< Authentication is complete */
    };

    enum class SSLState
    {
        NOT_CAPABLE,
        INCOMPLETE,
        COMPLETE,
        FAIL
    };

    enum class RoutingState
    {
        PACKET_START,           /**< Expecting the client to send a normal packet */
        LARGE_PACKET,           /**< Expecting the client to continue streaming a large packet */
        LARGE_HISTORY_PACKET,   /**< The client will continue writing a large command that is recorded */
        LOAD_DATA,              /**< Expecting the client to continue streaming CSV-data */
        CHANGING_DB,            /**< Client is changing database, waiting server response */
        CHANGING_ROLE,          /**< Client is changing role, waiting server response */
        CHANGING_USER,          /**< Session is changing user, waiting server response */
        RECORD_HISTORY,         /**< Recording a command and the result it generated */
        COMPARE_RESPONSES,      /**< Call callbacks that compare the recorded responses */
    };

    /** Data required during COM_CHANGE_USER. */
    struct ChangeUserFields
    {
        /**
         * The original change-user-packet from client. Given as-is to router, although backend protocol
         * will replace it with a generated packed. */
        mxs::Buffer client_query;

        /**
         * Authentication data. All client-side code should read this field for authentication-related data
         * when processing COM_CHANGE_USER. Backend code should always use the auth data in the protocol
         * session object. The backend authenticator will always read the most recent auth data when
         * connecting or sending COM_CHANGE_USER. This does not cause issues when replaying session commands,
         * as the command history is erased on COM_CHANGE_USER. */
        mariadb::SAuthData auth_data;

        /**
         * Backup of original auth data while waiting for server reply. This is required so the original
         * data can be restored if server replies with error. */
        mariadb::SAuthData auth_data_bu;
    };

    SSLState ssl_authenticate_check_status();
    int      ssl_authenticate_client();

    State        m_state {State::HANDSHAKING};                  /**< Overall state */
    HSState      m_handshake_state {HSState::INIT};             /**< Handshake state */
    AuthState    m_auth_state {AuthState::FIND_ENTRY};          /**< Authentication state */
    RoutingState m_routing_state {RoutingState::PACKET_START};  /**< Routing state */

    mariadb::SClientAuth m_authenticator;   /**< Client authentication data */
    ChangeUserFields     m_change_user;     /**< User account to change to */

    std::string m_pending_value;        /**< Role or db client is changing to */

    mxs::Component* m_downstream {nullptr}; /**< Downstream component, the session */
    MXS_SESSION*    m_session {nullptr};    /**< Generic session */
    MYSQL_session*  m_session_data {nullptr};
    qc_sql_mode_t   m_sql_mode {QC_SQL_MODE_DEFAULT};   /**< SQL-mode setting */
    uint8_t         m_sequence {0};                     /**< Latest sequence number from client */
    uint8_t         m_next_sequence {0};                /**< Next sequence to send to client */
    uint8_t         m_command {0};
    uint64_t        m_version {0};                  /**< Numeric server version */

    bool m_user_update_wakeup {false};      /**< Waking up because of user account update? */
    int  m_previous_userdb_version {0};     /**< Userdb version used for first user account search */

    std::vector<std::unique_ptr<LocalClient>> m_local_clients;

    int                      m_num_responses {0};   // How many responses we are waiting for
    uint32_t                 m_next_id {1};         // The next ID we'll use for a session command
    mxs::Buffer              m_pending_cmd;         // Current session command being executed
    size_t                   m_max_sescmd_history;  // Number of stored session commands
    mariadb::QueryClassifier m_qc;

    bool m_track_pooling_status {false};        /**< Does pooling status need to be tracked? */
    bool m_pooling_permanent_disable {false};   /**< Is pooling disabled permanently for this session? */
};
