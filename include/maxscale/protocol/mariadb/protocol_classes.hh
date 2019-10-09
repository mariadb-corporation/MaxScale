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
#include <maxscale/authenticator2.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/session.hh>
#include <maxscale/target.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>

class GWBUF;
struct KillInfo;

/* Type of the kill-command sent by client. */
enum kill_type_t
{
    KT_CONNECTION = (1 << 0),
    KT_QUERY      = (1 << 1),
    KT_SOFT       = (1 << 2),
    KT_HARD       = (1 << 3)
};

/*
 * Data shared with authenticators
 */
class MYSQL_session : public MXS_SESSION::ProtocolData
{
public:

    /**
     * Contains client capabilities. The client sends this data in the handshake response-packet, and the
     * same data is sent to backends. Usually only the client protocol should write to these.
     */
    class ClientInfo
    {
    public:
        uint32_t m_client_capabilities {0};     /*< Basic client capabilities */
        uint32_t m_extra_capabilities {0};      /*< MariaDB 10.2 capabilities */

        /**
         * Connection character set (default latin1 ). Usually just one byte is needed. COM_CHANGE_USER
         * sends two. */
        uint16_t m_charset {0x8};
    };

    bool     ssl_capable() const;
    uint32_t client_capabilities() const;
    uint32_t extra_capabilitites() const;

    uint8_t client_sha1[MYSQL_SCRAMBLE_LEN] {0};    /*< SHA1(password) */
    char    user[MYSQL_USER_MAXLEN + 1] {'\0'};     /*< username       */
    char    db[MYSQL_DATABASE_MAXLEN + 1] {'\0'};   /*< database       */
    uint8_t next_sequence {0};                      /*< Next packet sequence */
    bool    changing_user {false};                  /*< True if a COM_CHANGE_USER is in progress */

    ClientInfo client_info;     /**< Client capabilities from handshake response packet */

    // Authentication token storage. Used by different authenticators.
    mxs::ClientAuthenticator::ByteVec auth_token;
};

class MariaDBClientConnection : public mxs::ClientConnectionBase
{
public:
    /** Return type of process_special_commands() */
    enum spec_com_res_t
    {
        RES_CONTINUE,   // No special command detected, proceed as normal.
        RES_END,        // Query handling completed, do not send to filters/router.
    };

    MariaDBClientConnection(MXS_SESSION* session, mxs::Component* component,
                            std::unique_ptr<mxs::ClientAuthenticator> authenticator);

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(GWBUF* buffer) override;

    bool    init_connection() override;
    void    finish_connection() override;
    int32_t connlimit(int limit) override;

    int64_t capabilities() const override
    {
        return CAP_BACKEND;
    }

    std::string current_db() const override;

    std::unique_ptr<mxs::BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    static bool parse_kill_query(char* query, uint64_t* thread_id_out, kill_type_t* kt_out,
                                 std::string* user_out);
    void           mxs_mysql_execute_kill(MXS_SESSION* issuer, uint64_t target_id, kill_type_t type);
    const uint8_t* scramble() const;

    // TODO: move to private
    mxs_auth_state_t protocol_auth_state {MXS_AUTH_STATE_INIT};     /*< Client authentication state */

private:
    int            perform_authentication(DCB* generic_dcb, GWBUF* read_buffer, int nbytes_read);
    int            perform_normal_read(DCB* dcb, GWBUF* read_buffer, uint32_t nbytes_read);
    void           store_client_information(DCB* dcb, GWBUF* buffer);
    int            route_by_statement(uint64_t capabilities, GWBUF** p_readbuf);
    spec_com_res_t process_special_commands(DCB* dcb, GWBUF* read_buffer, uint8_t cmd);
    bool           handle_change_user(bool* changed_user, GWBUF** packetbuf);
    bool           reauthenticate_client(MXS_SESSION* session, GWBUF* packetbuf);
    spec_com_res_t handle_query_kill(DCB* dcb, GWBUF* read_buffer, uint32_t packet_len);
    void           handle_authentication_errors(DCB* dcb, int auth_val, int packet_number);
    int            mysql_send_auth_error(DCB* dcb, int packet_number, const char* mysql_message);
    char*          create_auth_fail_str(const char* username, const char* hostaddr,
                                        bool password, const char* db, int);
    int    mysql_send_standard_error(DCB* dcb, int sequence, int errnum, const char* msg);
    GWBUF* mysql_create_standard_error(int sequence, int error_number, const char* msg);
    bool   send_auth_switch_request_packet();
    int    send_mysql_client_handshake(DCB* dcb);
    char*  handle_variables(MXS_SESSION* session, GWBUF** read_buffer);
    void   track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf);
    void   parse_and_set_trx_state(MXS_SESSION* ses, GWBUF* data);
    void   mxs_mysql_execute_kill_all_others(MXS_SESSION* issuer, uint64_t target_id,
                                             uint64_t keep_protocol_thread_id, kill_type_t type);
    void mxs_mysql_execute_kill_user(MXS_SESSION* issuer, const char* user, kill_type_t type);
    void execute_kill(MXS_SESSION* issuer, std::shared_ptr<KillInfo> info);
    int  ssl_authenticate_check_status(DCB* generic_dcb);
    void track_current_command(GWBUF* buf);

    std::unique_ptr<mxs::ClientAuthenticator> m_authenticator;      /**< Client authentication data */

    mxs::Component* m_downstream {nullptr}; /**< Downstream component, the session */
    MXS_SESSION*    m_session {nullptr};    /**< Generic session */
    MYSQL_session*  m_session_data {nullptr};

    uint8_t     m_command {0};
    bool        m_changing_user {false};
    bool        m_large_query {false};
    uint64_t    m_version {0};                  /**< Numeric server version */
    mxs::Buffer m_stored_query;                 /**< Temporarily stored queries */
    uint8_t     m_scramble[MYSQL_SCRAMBLE_LEN]; /**< Created server scramble */
};

class MariaDBBackendConnection : public mxs::BackendConnection
{
public:
    using Iter = mxs::Buffer::iterator;

    static std::unique_ptr<MariaDBBackendConnection>
    create(MXS_SESSION* session, mxs::Component* component,
           std::unique_ptr<mxs::BackendAuthenticator> authenticator);

    static std::unique_ptr<MariaDBBackendConnection>
    create_test_protocol(std::unique_ptr<mxs::BackendAuthenticator> authenticator);

    ~MariaDBBackendConnection() override;

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(GWBUF* buffer) override;

    bool    init_connection() override;
    void    finish_connection() override;
    bool    reuse_connection(BackendDCB* dcb, mxs::Component* upstream) override;
    bool    established() override;
    json_t* diagnostics_json() const override;

    /**
     *  Check every packet type, if is ok packet then parse it
     *
     *  @param buff Buffer, may contain multiple complete packets
     */
    void mxs_mysql_get_session_track_info(GWBUF* buff);

    void        set_dcb(DCB* dcb) override;
    BackendDCB* dcb() const override;

    uint64_t thread_id() const;
    uint32_t server_capabilities {0};   /**< Server capabilities TODO: private */

private:
    MariaDBBackendConnection(std::unique_ptr<mxs::BackendAuthenticator> authenticator);

    int    gw_read_and_write(DCB* dcb);
    int    backend_write_delayqueue(DCB* dcb, GWBUF* buffer);
    void   backend_set_delayqueue(DCB* dcb, GWBUF* queue);
    int    gw_change_user(DCB* dcb, MXS_SESSION* session, GWBUF* queue);
    void   gw_reply_on_error(DCB* dcb);
    int    gw_send_change_user_to_backend(DCB* backend);
    void   gw_send_proxy_protocol_header(BackendDCB* backend_dcb);
    int    handle_persistent_connection(BackendDCB* dcb, GWBUF* queue);
    GWBUF* gw_create_change_user_packet(const MYSQL_session* mses);
    void   do_handle_error(DCB* dcb, const char* errmsg, uint16_t errnum = 2003);
    void   prepare_for_write(DCB* dcb, GWBUF* buffer);
    int    mysql_send_com_quit(DCB* dcb, int sequence, GWBUF* buf);
    bool   read_complete_packet(DCB* dcb, GWBUF** readbuf);
    GWBUF* track_response(GWBUF** buffer);
    bool   mxs_mysql_is_result_set(GWBUF* buffer);
    bool   gw_read_backend_handshake(DCB* dcb, GWBUF* buffer);
    void   handle_error_response(DCB* plain_dcb, GWBUF* buffer);
    bool   session_ok_to_route(DCB* dcb);
    bool   complete_ps_response(GWBUF* buffer);
    bool   handle_auth_change_response(GWBUF* reply, DCB* dcb);
    int    send_mysql_native_password_response(DCB* dcb);
    bool   expecting_text_result();
    bool   expecting_ps_response();
    void   mxs_mysql_parse_ok_packet(GWBUF* buff, size_t packet_offset, size_t packet_len);
    int    gw_decode_mysql_server_handshake(uint8_t* payload);
    GWBUF* gw_generate_auth_response(bool with_ssl, bool ssl_established, uint64_t service_capabilities);

    mxs_auth_state_t handle_server_response(DCB* generic_dcb, GWBUF* buffer);
    mxs_auth_state_t gw_send_backend_auth(BackendDCB* dcb);

    uint32_t create_capabilities(bool with_ssl, bool db_specified, uint64_t capabilities);
    GWBUF*   process_packets(GWBUF** result);
    void     process_one_packet(Iter it, Iter end, uint32_t len);
    void     process_reply_start(Iter it, Iter end);
    void     process_result_start(Iter it, Iter end);
    void     process_ps_response(Iter it, Iter end);
    void     update_error(mxs::Buffer::iterator it, mxs::Buffer::iterator end);
    bool     consume_fetched_rows(GWBUF* buffer);
    void     track_query(GWBUF* buffer);
    void     set_reply_state(mxs::ReplyState state);

    /**
     * Set associated client protocol session and upstream. Should be called after creation or when swapping
     * sessions.
     *
     * @param session The new session to read client data from
     * @param upstream The new upstream to send server replies to
     */
    void assign_session(MXS_SESSION* session, mxs::Component* upstream);

    mxs_auth_state_t protocol_auth_state {MXS_AUTH_STATE_CONNECTED}; /**< Backend authentication state */

    std::unique_ptr<mxs::BackendAuthenticator> m_authenticator;     /**< Backend authentication data */

    uint64_t    m_thread_id {0};                /**< Backend thread id, received in backend handshake */
    uint8_t     m_scramble[MYSQL_SCRAMBLE_LEN]; /**< Server scramble, received in backend handshake */
    int         m_ignore_replies {0};           /**< How many replies should be discarded */
    bool        m_collect_result {false};       /**< Collect the next result set as one buffer */
    bool        m_track_state {false};          /**< Track session state */
    bool        m_skip_next {false};
    uint64_t    m_num_coldefs {0};
    uint32_t    m_num_eof_packets {0};  /**< Encountered eof packet number, used for check packet type */
    mxs::Buffer m_collectq;             /**< Used to collect results when resultset collection is requested */
    int64_t     m_ps_packets {0};
    bool        m_opening_cursor = false;   /**< Whether we are opening a cursor */
    uint32_t    m_expected_rows = 0;        /**< Number of rows a COM_STMT_FETCH is retrieving */
    bool        m_large_query = false;
    bool        m_changing_user {false};
    mxs::Reply  m_reply;

    mxs::Component* m_upstream {nullptr};       /**< Upstream component, typically a router */
    MXS_SESSION*    m_session {nullptr};        /**< Generic session */
    MYSQL_session*  m_client_data {nullptr};    /**< Client-session shared data */
    GWBUF*          m_stored_query {nullptr};   /*< Temporarily stored queries */
    BackendDCB*     m_dcb {nullptr};            /**< Dcb used by this protocol connection */
};

bool     is_last_ok(MariaDBBackendConnection::Iter it);
bool     is_last_eof(MariaDBBackendConnection::Iter it);
uint64_t get_encoded_int(MariaDBBackendConnection::Iter it);

class MySQLProtocolModule : public mxs::ProtocolModule
{
public:
    static MySQLProtocolModule* create(const std::string& auth_name, const std::string& auth_opts);

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override;

    std::string auth_default() const override;
    GWBUF*      reject(const std::string& host) override;


    std::string name() const override;

    int  load_auth_users(SERVICE* service) override;
    void print_auth_users(DCB* output) override;

    json_t* print_auth_users_json() override;

private:
    std::unique_ptr<mxs::AuthenticatorModule> m_auth_module;
};
