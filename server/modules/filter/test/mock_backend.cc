/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/mock/backend.hh"
#include <algorithm>
#include <vector>
#include <maxscale/query_classifier.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/resultset.hh>
#include <iostream>

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
GWBUF* Backend::create_ok_response()
{
    /* Note: sequence id is always 01 (4th byte) */
    const static uint8_t ok[MYSQL_OK_PACKET_MIN_LEN] =
    {07, 00, 00, 01, 00, 00, 00, 02, 00, 00, 00};

    GWBUF* pResponse = gwbuf_alloc_and_load(sizeof(ok), &ok);
    mxb_assert(pResponse);

    return pResponse;
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
    GWBUF* pResponse = dequeue_response(pSession, &empty);

    if (pResponse)
    {
        mxs::ReplyRoute down;
        pSession->clientReply(pResponse, down, reply);
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
    gwbuf_free(dequeue_response(pSession, &empty));

    return !empty;
}

void BufferBackend::discard_all_responses(const RouterSession* pSession)
{
    mxb_assert(!idle(pSession));

    if (!idle(pSession))
    {
        Responses& responses = m_session_responses[pSession];
        mxb_assert(!responses.empty());

        std::for_each(responses.begin(), responses.end(), gwbuf_free);
        responses.clear();
    }
}

void BufferBackend::enqueue_response(const RouterSession* pSession, GWBUF* pResponse)
{
    Responses& responses = m_session_responses[pSession];

    responses.push_back(pResponse);
}

GWBUF* BufferBackend::dequeue_response(const RouterSession* pSession, bool* pEmpty)
{
    mxb_assert(!idle(pSession));
    GWBUF* pResponse = NULL;
    *pEmpty = true;

    if (!idle(pSession))
    {
        Responses& responses = m_session_responses[pSession];
        mxb_assert(!responses.empty());

        if (!responses.empty())
        {
            pResponse = responses.front();
            responses.pop_front();
        }

        *pEmpty = responses.empty();
    }

    return pResponse;
}


//
// OkBackend
//

OkBackend::OkBackend()
{
}

void OkBackend::handle_statement(RouterSession* pSession, GWBUF* pStatement)
{
    enqueue_response(pSession, create_ok_response());

    gwbuf_free(pStatement);
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

    GWBUF* create_response() const
    {
        return gwbuf_alloc_and_load(m_response.size(), &m_response.front());
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

        void write_ready(DCB*) override
        {
            mxb_assert(!true);
        }

        void error(DCB*) override
        {
            mxb_assert(!true);
        }

        void hangup(DCB*) override
        {
            mxb_assert(!true);
        }

        int32_t write(GWBUF* pBuffer) override
        {
            return m_owner.write(pBuffer);
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

        int32_t clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override
        {
            return write(buffer);
        }

    private:
        ResultSetDCB& m_owner;
    };

    friend class Protocol;

public:
    Protocol* protocol() const override
    {
        return &m_protocol;
    }

private:
    int32_t write(GWBUF* pBuffer)
    {
        pBuffer = gwbuf_make_contiguous(pBuffer);
        mxb_assert(pBuffer);

        unsigned char* begin = GWBUF_DATA(pBuffer);
        unsigned char* end = begin + GWBUF_LENGTH(pBuffer);

        m_response.insert(m_response.end(), begin, end);

        gwbuf_free(pBuffer);
        return 1;
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

void ResultSetBackend::handle_statement(RouterSession* pSession, GWBUF* pStatement)
{
    qc_query_op_t op = qc_get_operation(pStatement);
    gwbuf_free(pStatement);

    if (op == QUERY_OP_SELECT)
    {
        std::unique_ptr<ResultSet> set = ResultSet::create({"a"});
        set->add_row({std::to_string(++m_counter)});
        ResultSetDCB dcb(pSession->session());
        dcb.protocol()->write(set->as_buffer().release());

        enqueue_response(pSession, dcb.create_response());
    }
    else
    {
        enqueue_response(pSession, create_ok_response());
    }
}
}   // mock
}   // maxscale
