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

class MariaDBBackendConnection : public mxs::BackendConnection
{
public:
    using Iter = mxs::Buffer::iterator;

    static std::unique_ptr<MariaDBBackendConnection>
    create(MXS_SESSION* session, mxs::Component* component, mariadb::SBackendAuth authenticator);

    static std::unique_ptr<MariaDBBackendConnection>
    create_test_protocol(mariadb::SBackendAuth authenticator);

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
    void    ping() override;
    int64_t seconds_idle() const override;
    json_t* diagnostics() const override;

    /**
     *  Check every packet type, if is ok packet then parse it
     *
     *  @param buff Buffer, may contain multiple complete packets
     */
    void mxs_mysql_get_session_track_info(GWBUF* buff);

    void              set_dcb(DCB* dcb) override;
    const BackendDCB* dcb() const override;
    BackendDCB*       dcb() override;

    uint64_t thread_id() const;
    uint32_t server_capabilities {0};   /**< Server capabilities TODO: private */

private:
    enum class AuthState
    {
        CONNECTED,      /**< Network connection to server created */
        RESPONSE_SENT,  /**< Responded to the read authentication message */
        FAIL,           /**< Authentication failed */
        FAIL_HANDSHAKE, /**< Authentication failed immediately */
        COMPLETE,       /**< Authentication is complete */
    };

    static std::string to_string(AuthState auth_state);

    MariaDBBackendConnection(mariadb::SBackendAuth authenticator);

    int    gw_read_and_write(DCB* dcb);
    bool   backend_write_delayqueue(DCB* dcb, GWBUF* buffer);
    void   backend_set_delayqueue(DCB* dcb, GWBUF* queue);
    bool   change_user(DCB* backend, GWBUF* queue);
    bool   send_change_user_to_backend(DCB* backend);
    void   gw_send_proxy_protocol_header(BackendDCB* backend_dcb);
    int    handle_persistent_connection(BackendDCB* dcb, GWBUF* queue);
    GWBUF* gw_create_change_user_packet();
    void   do_handle_error(DCB* dcb, const std::string& errmsg,
                           mxs::ErrorType type = mxs::ErrorType::TRANSIENT);
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

    AuthState handle_server_response(DCB* generic_dcb, GWBUF* buffer);
    AuthState gw_send_backend_auth(BackendDCB* dcb);

    uint32_t create_capabilities(bool with_ssl, bool db_specified, uint64_t capabilities);
    GWBUF*   process_packets(GWBUF** result);
    void     process_one_packet(Iter it, Iter end, uint32_t len);
    void     process_reply_start(Iter it, Iter end);
    void     process_result_start(Iter it, Iter end);
    void     process_ps_response(Iter it, Iter end);
    void     process_ok_packet(Iter it, Iter end);
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

    mariadb::SBackendAuth m_authenticator;                      /**< Backend authentication data */

    AuthState   m_auth_state {AuthState::CONNECTED};/**< Backend authentication state */
    uint64_t    m_thread_id {0};                    /**< Backend thread id, received in backend handshake */
    uint8_t     m_scramble[MYSQL_SCRAMBLE_LEN];     /**< Server scramble, received in backend handshake */
    int         m_ignore_replies {0};               /**< How many replies should be discarded */
    bool        m_collect_result {false};           /**< Collect the next result set as one buffer */
    bool        m_track_state {false};              /**< Track session state */
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
