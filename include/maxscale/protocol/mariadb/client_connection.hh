/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

struct KillInfo;

/* Type of the kill-command sent by client. */
enum kill_type_t
{
    KT_CONNECTION = (1 << 0),
    KT_QUERY      = (1 << 1),
    KT_SOFT       = (1 << 2),
    KT_HARD       = (1 << 3)
};

class MariaDBUserManager;

class MariaDBClientConnection : public mxs::ClientConnectionBase
{
public:
    /** Return type of process_special_commands() */
    enum spec_com_res_t
    {
        RES_CONTINUE,   // No special command detected, proceed as normal.
        RES_END,        // Query handling completed, do not send to filters/router.
    };

    enum class AuthState
    {
        INIT,           /**< Initial authentication state */
        MSG_READ,       /**< Read an authentication message from the server */
        RESPONSE_SENT,  /**< Responded to the read authentication message */
        FAIL,           /**< Authentication failed */
        COMPLETE,       /**< Authentication is complete */
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

    std::string current_db() const override;

    static bool parse_kill_query(char* query, uint64_t* thread_id_out, kill_type_t* kt_out,
                                 std::string* user_out);
    void           mxs_mysql_execute_kill(MXS_SESSION* issuer, uint64_t target_id, kill_type_t type);
    const uint8_t* scramble() const;

    // TODO: move to private
    AuthState m_auth_state {AuthState::INIT};       /*< Client authentication state */

private:
    int            perform_authentication(GWBUF* read_buffer, int nbytes_read);
    int            perform_normal_read(GWBUF* read_buffer, uint32_t nbytes_read);
    void           store_client_information(GWBUF* buffer);
    int            route_by_statement(uint64_t capabilities, GWBUF** p_readbuf);
    spec_com_res_t process_special_commands(DCB* dcb, GWBUF* read_buffer, uint8_t cmd);
    bool           handle_change_user(bool* changed_user, GWBUF** packetbuf);
    bool           reauthenticate_client(MXS_SESSION* session, GWBUF* packetbuf);
    spec_com_res_t handle_query_kill(DCB* dcb, GWBUF* read_buffer, uint32_t packet_len);
    void           handle_use_database(GWBUF* read_buffer);
    void           handle_authentication_errors(DCB* dcb, mariadb::ClientAuthenticator::AuthRes auth_val,
                                                int packet_number);
    int   mysql_send_auth_error(DCB* dcb, int packet_number, const char* mysql_message);
    char* create_auth_fail_str(const char* username, const char* hostaddr,
                               bool password, const char* db,
                               mariadb::ClientAuthenticator::AuthRes error);
    int    mysql_send_standard_error(DCB* dcb, int sequence, int errnum, const char* msg);
    GWBUF* mysql_create_standard_error(int sequence, int error_number, const char* msg);
    bool   send_auth_switch_request_packet();
    int    send_mysql_client_handshake();
    char*  handle_variables(MXS_SESSION* session, GWBUF** read_buffer);
    void   track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf);
    void   mxs_mysql_execute_kill_all_others(MXS_SESSION* issuer, uint64_t target_id,
                                             uint64_t keep_protocol_thread_id, kill_type_t type);
    void  mxs_mysql_execute_kill_user(MXS_SESSION* issuer, const char* user, kill_type_t type);
    void  execute_kill(MXS_SESSION* issuer, std::shared_ptr<KillInfo> info);
    void  track_current_command(GWBUF* buf);

    const MariaDBUserManager*             user_account_manager();
    mariadb::ClientAuthenticator::AuthRes ssl_authenticate_check_status(DCB* generic_dcb);
    static std::string                    to_string(AuthState state);

    mariadb::SClientAuth m_authenticator;       /**< Client authentication data */
    std::unique_ptr<UserEntry> m_user_entry;    /**< Client user entry */

    mxs::Component* m_downstream {nullptr}; /**< Downstream component, the session */
    MXS_SESSION*    m_session {nullptr};    /**< Generic session */
    MYSQL_session*  m_session_data {nullptr};
    qc_sql_mode_t   m_sql_mode {QC_SQL_MODE_DEFAULT};   /**< SQL-mode setting */

    uint8_t     m_command {0};
    bool        m_changing_user {false};
    bool        m_large_query {false};
    uint64_t    m_version {0};                  /**< Numeric server version */
    mxs::Buffer m_stored_query;                 /**< Temporarily stored queries */
    uint8_t     m_scramble[MYSQL_SCRAMBLE_LEN]; /**< Created server scramble */
};
