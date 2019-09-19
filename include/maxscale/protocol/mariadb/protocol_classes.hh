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
#include <maxscale/target.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>

class GWBUF;
struct KillInfo;
struct MYSQL_session;

/* Type of the kill-command sent by client. */
enum kill_type_t
{
    KT_CONNECTION = (1 << 0),
    KT_QUERY      = (1 << 1),
    KT_SOFT       = (1 << 2),
    KT_HARD       = (1 << 3)
};

/**
 * MySQL Protocol specific state data. Tracks various parts of the network protocol e.g. the response state.
 */
class MySQLProtocol
{
public:
    MySQLProtocol(MXS_SESSION* session, SERVER* server);
    ~MySQLProtocol();

    /**
     * Track a client query
     *
     * Inspects the query and tracks the current command being executed. Also handles detection of
     * multi-packet requests and the special handling that various commands need.
     */
    void track_query(GWBUF* buffer);

    /**
     * Get the reply state object
     */
    const mxs::Reply& reply() const
    {
        return m_reply;
    }

    /**
     * Get the session the protocol data is associated with.
     *
     * @return A session.
     */
    MXS_SESSION* session() const
    {
        return m_session;
    }

    //
    // Legacy public members
    //
    uint8_t  scramble[MYSQL_SCRAMBLE_LEN];  /*< server scramble, created or received */
    uint32_t client_capabilities = 0;       /*< client capabilities, created or received */
    uint32_t extra_capabilities = 0;        /*< MariaDB 10.2 capabilities */

    unsigned int charset = 0x8;             /*< Connection character set (default latin1 )*/
    GWBUF*       stored_query = nullptr;    /*< Temporarily stored queries */
    bool         changing_user = false;

    //
    // END Legacy public members
    //

    using Iter = mxs::Buffer::iterator;

protected:
    MXS_SESSION* m_session;                 /**< The session this protocol session is associated with */
    bool         m_opening_cursor = false;  /**< Whether we are opening a cursor */
    uint32_t     m_expected_rows = 0;       /**< Number of rows a COM_STMT_FETCH is retrieving */
    bool         m_large_query = false;
    mxs::Reply   m_reply;

    uint64_t m_version;     // Numeric server version

    inline void set_reply_state(mxs::ReplyState state)
    {
        m_reply.set_reply_state(state);
    }
};

class MySQLClientProtocol : public MySQLProtocol, public mxs::ClientProtocol
{
public:
    /** Return type of process_special_commands() */
    enum spec_com_res_t
    {
        RES_CONTINUE,   // No special command detected, proceed as normal.
        RES_END,        // Query handling completed, do not send to filters/router.
    };

    static MySQLClientProtocol* create(MXS_SESSION* session, mxs::Component* component);
    MySQLClientProtocol(MXS_SESSION* session, SERVER* server, mxs::Component* component,
                        std::unique_ptr<mxs::ClientAuthenticator> authenticator);

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(DCB* dcb, GWBUF* buffer) override;

    bool    init_connection(DCB* dcb) override;
    void    finish_connection(DCB* dcb) override;
    int32_t connlimit(DCB* dcb, int limit) override;

    int64_t capabilities() const override
    {
        return CAP_BACKEND;
    }

    std::unique_ptr<mxs::BackendProtocol>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    static bool parse_kill_query(char* query, uint64_t* thread_id_out, kill_type_t* kt_out,
                                 std::string* user_out);
    void mxs_mysql_execute_kill(MXS_SESSION* issuer, uint64_t target_id, kill_type_t type);

    // TODO: move to private
    mxs_auth_state_t protocol_auth_state {MXS_AUTH_STATE_INIT};   /*< Client authentication state */

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
    int            mysql_send_standard_error(DCB* dcb, int sequence, int errnum, const char* msg);
    GWBUF*         mysql_create_standard_error(int sequence, int error_number, const char* msg);
    bool           send_auth_switch_request_packet(DCB* dcb);
    int            send_mysql_client_handshake(DCB* dcb);
    char*          handle_variables(MXS_SESSION* session, GWBUF** read_buffer);
    void           track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf);
    void           parse_and_set_trx_state(MXS_SESSION* ses, GWBUF* data);
    void           mxs_mysql_execute_kill_all_others(MXS_SESSION* issuer, uint64_t target_id,
                                                     uint64_t keep_protocol_thread_id, kill_type_t type);
    void           mxs_mysql_execute_kill_user(MXS_SESSION* issuer, const char* user, kill_type_t type);
    void           execute_kill(MXS_SESSION* issuer, std::shared_ptr<KillInfo> info);
    int            ssl_authenticate_check_status(DCB* generic_dcb);

    mxs::Component* m_component {nullptr}; /**< Downstream component, the session */
    std::unique_ptr<mxs::ClientAuthenticator> m_authenticator;  /**< Client authentication data */
};

class MySQLBackendProtocol : public MySQLProtocol, public mxs::BackendProtocol
{
public:
    static std::unique_ptr<MySQLBackendProtocol>
    create(MXS_SESSION* session, SERVER* server, const MySQLClientProtocol& client_protocol,
           mxs::Component* component, std::unique_ptr<mxs::BackendAuthenticator> authenticator);

    MySQLBackendProtocol(MXS_SESSION* session, SERVER* server, mxs::Component* component,
                         std::unique_ptr<mxs::BackendAuthenticator> authenticator);

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(DCB* dcb, GWBUF* buffer) override;

    bool    init_connection(DCB* dcb) override;
    void    finish_connection(DCB* dcb) override;
    bool    reuse_connection(BackendDCB* dcb, mxs::Component* upstream) override;
    bool    established(DCB*) override;
    json_t* diagnostics_json(DCB* dcb) override;

    /**
     *  Check every packet type, if is ok packet then parse it
     *
     *  @param buff Buffer, may contain multiple complete packets
     */
    void mxs_mysql_get_session_track_info(GWBUF* buff);

    uint64_t thread_id() const;
    uint32_t server_capabilities {0};   /**< Server capabilities TODO: private */

private:
    int              gw_read_and_write(DCB* dcb);
    int              backend_write_delayqueue(DCB* dcb, GWBUF* buffer);
    void             backend_set_delayqueue(DCB* dcb, GWBUF* queue);
    int              gw_change_user(DCB* dcb, MXS_SESSION* session, GWBUF* queue);
    void             gw_reply_on_error(DCB* dcb);
    int              gw_send_change_user_to_backend(DCB* backend);
    void             gw_send_proxy_protocol_header(BackendDCB* backend_dcb);
    int              handle_persistent_connection(BackendDCB* dcb, GWBUF* queue);
    GWBUF*           gw_create_change_user_packet(MYSQL_session* mses);
    void             do_handle_error(DCB* dcb, const char* errmsg);
    void             prepare_for_write(DCB* dcb, GWBUF* buffer);
    mxs_auth_state_t handle_server_response(DCB* generic_dcb, GWBUF* buffer);
    int              mysql_send_com_quit(DCB* dcb, int sequence, GWBUF* buf);
    bool             read_complete_packet(DCB* dcb, GWBUF** readbuf);
    GWBUF*           track_response(GWBUF** buffer);
    bool             mxs_mysql_is_result_set(GWBUF* buffer);
    mxs_auth_state_t gw_send_backend_auth(BackendDCB* dcb);
    bool             gw_read_backend_handshake(DCB* dcb, GWBUF* buffer);
    void             handle_error_response(DCB* plain_dcb, GWBUF* buffer);
    bool             session_ok_to_route(DCB* dcb);
    bool             complete_ps_response(GWBUF* buffer);
    bool             handle_auth_change_response(GWBUF* reply, DCB* dcb);
    int              send_mysql_native_password_response(DCB* dcb);
    bool             expecting_text_result();
    bool             expecting_ps_response();
    void             mxs_mysql_parse_ok_packet(GWBUF* buff, size_t packet_offset, size_t packet_len);
    int              gw_decode_mysql_server_handshake(uint8_t* payload);
    GWBUF*           gw_generate_auth_response(MYSQL_session* client, bool with_ssl, bool ssl_established,
                                               uint64_t service_capabilities);

    uint32_t create_capabilities(bool with_ssl, bool db_specified, uint64_t capabilities);
    GWBUF*   process_packets(GWBUF** result);
    void     process_one_packet(Iter it, Iter end, uint32_t len);
    void     process_reply_start(Iter it, Iter end);
    void     update_error(mxs::Buffer::iterator it, mxs::Buffer::iterator end);
    bool     consume_fetched_rows(GWBUF* buffer);

    mxs_auth_state_t protocol_auth_state {MXS_AUTH_STATE_INIT}; /**< Backend authentication state */
    mxs::Component*  m_component {nullptr};                     /**< Upstream component, typically a router */

    std::unique_ptr<mxs::BackendAuthenticator> m_authenticator;  /**< Backend authentication data */

    uint64_t    m_thread_id {0};            /**< Backend thread id, received in backend handshake */
    uint16_t    m_modutil_state;            /**< TODO: This is an ugly hack, replace it */
    int         m_ignore_replies {0};       /**< How many replies should be discarded */
    bool        m_collect_result {false};   /**< Collect the next result set as one buffer */
    bool        m_track_state {false};      /**< Track session state */
    bool        m_skip_next {false};
    uint64_t    m_num_coldefs {0};
    uint32_t    m_num_eof_packets {0};  /**< Encountered eof packet number, used for check packet type */
    mxs::Buffer m_collectq;             /**< Used to collect results when resultset collection is requested */
};

/*
 * MySQL session specific data
 */
struct MYSQL_session
{
    uint8_t  client_sha1[MYSQL_SCRAMBLE_LEN];   /*< SHA1(password) */
    char     user[MYSQL_USER_MAXLEN + 1];       /*< username       */
    char     db[MYSQL_DATABASE_MAXLEN + 1];     /*< database       */
    int      auth_token_len;                    /*< token length   */
    uint8_t* auth_token;                        /*< token          */
    bool     correct_authenticator;             /*< is session using mysql_native_password? */
    uint8_t  next_sequence;                     /*< Next packet sequence */
    bool     auth_switch_sent;                  /*< Expecting a response to AuthSwitchRequest? */
    bool     changing_user;                     /*< True if a COM_CHANGE_USER is in progress */
};

bool is_last_ok(MySQLProtocol::Iter it);
bool is_last_eof(MySQLProtocol::Iter it);
uint64_t get_encoded_int(MySQLProtocol::Iter it);

/**
 * Allocate a new MySQL_session
 *
 * @return New MySQL_session or NULL if memory allocation failed
 */
MYSQL_session* mysql_session_alloc();

bool gw_get_shared_session_auth_info(DCB* dcb, MYSQL_session* session);
