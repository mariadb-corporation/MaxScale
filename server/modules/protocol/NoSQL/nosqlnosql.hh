/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include "nosqlcommand.hh"


namespace nosql
{

class Database;
class UserManager;

class NoSQL
{
public:
    NoSQL(MXS_SESSION*      pSession,
          ClientConnection* pClient_connection,
          mxs::Component*   pDownstream,
          Config*           pConfig,
          UserManager*      pUm);
    ~NoSQL();

    NoSQL(const NoSQL&) = delete;
    NoSQL& operator = (const NoSQL&) = delete;

    State state() const
    {
        return m_sDatabase ?  State::BUSY : State::READY;
    }

    bool is_busy() const
    {
        return state() == State::BUSY;
    }

    Context& context()
    {
        return m_context;
    }

    const Config& config() const
    {
        return m_config;
    }

    GWBUF* current_request() const
    {
        return m_pCurrent_request;
    }

    State handle_request(GWBUF* pRequest);

    bool clientReply(GWBUF&& mariadb_response, const mxs::ReplyRoute& down, const mxs::Reply& reply);

    void set_dcb(DCB* pDcb)
    {
        mxb_assert(!m_pDcb);
        m_pDcb = pDcb;
    }

    CacheFilterSession* cache_filter_session() const
    {
        return m_pCache_filter_session;
    }

    void set_cache_filter_session(CacheFilterSession* pCache_filter_session)
    {
        mxb_assert(!m_pCache_filter_session);
        m_pCache_filter_session = pCache_filter_session;
    }

private:
    template<class T>
    void log_in(const char* zContext, const T& req)
    {
        if (m_config.should_log_in())
        {
            MXB_NOTICE("%s: %s", zContext, req.to_string().c_str());
        }
    }

    void kill_client();

    using SDatabase = std::unique_ptr<Database>;

    State handle_delete(GWBUF* pRequest, packet::Delete&& req, Command::Response* pResponse);
    State handle_insert(GWBUF* pRequest, packet::Insert&& req, Command::Response* pResponse);
    State handle_update(GWBUF* pRequest, packet::Update&& req, Command::Response* pResponse);
    State handle_query(GWBUF* pRequest, packet::Query&& req, Command::Response* pResponse);
    State handle_get_more(GWBUF* pRequest, packet::GetMore&& req, Command::Response* pResponse);
    State handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, Command::Response* pResponse);
    State handle_msg(GWBUF* pRequest, packet::Msg&& req, Command::Response* pResponse);

    void flush_response(Command::Response& response);

    Context             m_context;
    Config&             m_config;
    std::deque<GWBUF*>  m_requests;
    SDatabase           m_sDatabase;
    DCB*                m_pDcb { nullptr };
    GWBUF*              m_pCurrent_request { nullptr };
    CacheFilterSession* m_pCache_filter_session { nullptr };
};

}
