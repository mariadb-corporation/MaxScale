/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "maxscale/mock/backend.hh"
#include <algorithm>
#include <iostream>
#include <vector>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/resultset.hh>

using namespace std;

namespace maxscale
{

namespace mock
{

//
// Backend
//

Backend::Backend()
{
}

Backend::~Backend()
{
}

// static
GWBUF Backend::create_ok_response()
{
    /* Note: sequence id is always 01 (4th byte) */
    const static uint8_t ok[MYSQL_OK_PACKET_MIN_LEN] =
    {07, 00, 00, 01, 00, 00, 00, 02, 00, 00, 00};

    return GWBUF{ok, sizeof(ok)};
}

//
// BufferBackend
//
BufferBackend::BufferBackend()
{
}

BufferBackend::~BufferBackend()
{
}

bool BufferBackend::respond(RouterSession* pSession, const mxs::Reply& reply)
{
    bool empty = false;
    GWBUF response = dequeue_response(pSession, &empty);

    if (response)
    {
        mxs::ReplyRoute down;
        pSession->clientReply(std::move(response), down, reply);
    }

    return !empty;
}

bool BufferBackend::idle(const RouterSession* pSession) const
{
    bool rv = true;

    SessionResponses::const_iterator i = m_session_responses.find(pSession);

    if (i != m_session_responses.end())
    {
        const Responses& responses = i->second;
        rv = responses.empty();
    }

    return rv;
}

bool BufferBackend::discard_one_response(const RouterSession* pSession)
{
    bool empty = false;
    dequeue_response(pSession, &empty);

    return !empty;
}

void BufferBackend::discard_all_responses(const RouterSession* pSession)
{
    mxb_assert(!idle(pSession));

    if (!idle(pSession))
    {
        Responses& responses = m_session_responses[pSession];
        mxb_assert(!responses.empty());
        responses.clear();
    }
}

void BufferBackend::enqueue_response(const RouterSession* pSession, GWBUF&& response)
{
    m_session_responses[pSession].emplace_back(std::move(response));
}

GWBUF BufferBackend::dequeue_response(const RouterSession* pSession, bool* pEmpty)
{
    mxb_assert(!idle(pSession));
    GWBUF response;
    *pEmpty = true;

    if (!idle(pSession))
    {
        Responses& responses = m_session_responses[pSession];
        mxb_assert(!responses.empty());

        if (!responses.empty())
        {
            response = std::move(responses.front());
            responses.pop_front();
        }

        *pEmpty = responses.empty();
    }

    return response;
}


//
// OkBackend
//

OkBackend::OkBackend()
{
}

void OkBackend::handle_statement(RouterSession* pSession, GWBUF&& statement)
{
    enqueue_response(pSession, create_ok_response());
}

//
// ResultSetBackend
//
ResultSetBackend::ResultSetBackend()
    : m_counter(0)
    , m_created(false)
{
}

namespace
{

class ResultSetDCB : public ClientDCB
{
public:
    ResultSetDCB(MXS_SESSION* session)
        : ClientDCB(DCB::FD_CLOSED, "127.0.0.1", sockaddr_storage {}, DCB::Role::CLIENT, session,
                    nullptr, nullptr),
        m_protocol(this)
    {
    }

    GWBUF create_response() const
    {
        return GWBUF(reinterpret_cast<const uint8_t*>(m_response.data()), m_response.size());
    }

private:
    class Protocol : public mxs::ClientConnection
    {
    public:
        Protocol(ResultSetDCB* pOwner)
            : m_owner(*pOwner)
        {
        }

        bool init_connection() override
        {
            mxb_assert(!true);
            return false;
        }

        void finish_connection() override
        {
            mxb_assert(!true);
        }

        void ready_for_reading(DCB*) override
        {
            mxb_assert(!true);
        }

        void error(DCB*, const char* errmsg) override
        {
            mxb_assert(!true);
        }

        json_t* diagnostics() const override
        {
            return nullptr;
        }

        void set_dcb(DCB* dcb) override
        {
            // m_owner is set in ctor
        }

        ClientDCB* dcb() override
        {
            return &m_owner;
        }

        const ClientDCB* dcb() const override
        {
            return &m_owner;
        }

        bool in_routing_state() const override
        {
            return true;
        }

        bool safe_to_restart() const override
        {
            return true;
        }

        bool clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override
        {
            return write(std::move(buffer));
        }

        size_t sizeof_buffers() const override
        {
            return 0;
        }

    private:
        bool write(GWBUF&& buffer)
        {
            return m_owner.write(std::move(buffer));
        }

        ResultSetDCB& m_owner;
    };

    friend class Protocol;

public:
    Protocol* protocol() const override
    {
        return &m_protocol;
    }

private:
    bool write(GWBUF&& buffer)
    {
        m_response.insert(m_response.end(), buffer.begin(), buffer.end());
        return true;
    }

    mutable Protocol  m_protocol;
    std::vector<char> m_response;
};
}

bool ResultSetBackend::respond(RouterSession* pSession, const mxs::Reply& reply)
{
    mxs::Reply r;
    r.add_field_count(1);
    return BufferBackend::respond(pSession, r);
}

void ResultSetBackend::handle_statement(RouterSession* pSession, GWBUF&& statement)
{
    mxs::sql::OpCode op = MariaDBParser::get().get_operation(statement);

    if (op == mxs::sql::OP_SELECT)
    {
        std::unique_ptr<ResultSet> set = ResultSet::create({"a"});
        set->add_row({std::to_string(++m_counter)});
        ResultSetDCB dcb(pSession->session());
        dcb.protocol()->clientReply(set->as_buffer(), mxs::ReplyRoute {}, mxs::Reply {});

        enqueue_response(pSession, dcb.create_response());
    }
    else
    {
        enqueue_response(pSession, create_ok_response());
    }
}
}   // mock
}   // maxscale
