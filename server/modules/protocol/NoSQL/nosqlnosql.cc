/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlnosql.hh"
#include <sstream>
#include "clientconnection.hh"
#include "nosqlcommand.hh"
#include "nosqldatabase.hh"

using namespace std;

namespace
{

string extract_database(const string& collection)
{
    auto i = collection.find('.');

    if (i == string::npos)
    {
        return collection;
    }

    return collection.substr(0, i);
}

}

namespace nosql
{

NoSQL::NoSQL(MXS_SESSION* pSession,
             ClientConnection* pClient_connection,
             mxs::Component* pDownstream,
             Config* pConfig,
             UserManager* pUm)
    : m_context(pUm, pSession, pClient_connection, pDownstream)
    , m_config(*pConfig)
{
}

NoSQL::~NoSQL()
{
}

void NoSQL::handle_request(GWBUF* pRequest)
{
    GWBUF* pResponse = nullptr;
    handle_request(pRequest, &pResponse);

    if (pResponse)
    {
        m_pDcb->writeq_append(mxs::gwbufptr_to_gwbuf(pResponse));
    }
}

State NoSQL::handle_request(GWBUF* pRequest, GWBUF** ppResponse)
{
    State state = State::READY;
    GWBUF* pResponse = nullptr;

    if (!m_sDatabase)
    {
        m_pCurrent_request = pRequest;

        try
        {
            // If no database operation is in progress, we proceed.
            packet::Packet req(pRequest);

            mxb_assert(req.msg_len() == (int)pRequest->length());

            switch (req.opcode())
            {
            case MONGOC_OPCODE_COMPRESSED:
            case MONGOC_OPCODE_REPLY:
                {
                    ostringstream ss;
                    ss << "Unsupported packet " << nosql::opcode_to_string(req.opcode()) << " received.";
                    throw std::runtime_error(ss.str());
                }
                break;

            case MONGOC_OPCODE_GET_MORE:
                state = handle_get_more(pRequest, packet::GetMore(req), &pResponse);
                break;

            case MONGOC_OPCODE_KILL_CURSORS:
                state = handle_kill_cursors(pRequest, packet::KillCursors(req), &pResponse);
                break;

            case MONGOC_OPCODE_DELETE:
                state = handle_delete(pRequest, packet::Delete(req), &pResponse);
                break;

            case MONGOC_OPCODE_INSERT:
                state = handle_insert(pRequest, packet::Insert(req), &pResponse);
                break;

            case MONGOC_OPCODE_MSG:
                state = handle_msg(pRequest, packet::Msg(req), &pResponse);
                break;

            case MONGOC_OPCODE_QUERY:
                state = handle_query(pRequest, packet::Query(req), &pResponse);
                break;

            case MONGOC_OPCODE_UPDATE:
                state = handle_update(pRequest, packet::Update(req), &pResponse);
                break;

            default:
                {
                    mxb_assert(!true);
                    ostringstream ss;
                    ss << "Unknown packet " << req.opcode() << " received.";
                    throw std::runtime_error(ss.str());
                }
            }
        }
        catch (const std::exception& x)
        {
            MXB_ERROR("Closing client connection: %s", x.what());
            kill_client();
        }

        m_pCurrent_request = nullptr;

        gwbuf_free(pRequest);
    }
    else
    {
        // Otherwise we push it on the request queue.
        m_requests.push_back(pRequest);
    }

    *ppResponse = pResponse;
    return state;
}

bool NoSQL::clientReply(GWBUF&& response, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(m_pDcb);
    mxb_assert(m_sDatabase.get());

    Command::Response protocol_response = m_sDatabase->translate(std::move(response));

    if (m_sDatabase->is_ready())
    {
        m_sDatabase.reset();

        if (protocol_response)
        {
            m_pDcb->writeq_append(mxs::gwbufptr_to_gwbuf(protocol_response.release()));
        }

        if (!m_requests.empty())
        {
            // Loop as long as responses to requests can be generated immediately.
            // If it can't then we'll continue once clientReply() is called anew.
            State state = State::READY;
            do
            {
                mxb_assert(!m_sDatabase.get());

                GWBUF* pRequest = m_requests.front();
                m_requests.pop_front();

                GWBUF* pData = nullptr;
                state = handle_request(pRequest, &pData);

                protocol_response.reset(pData);

                if (protocol_response)
                {
                    // The response could be generated immediately, just send it.
                    m_pDcb->writeq_append(mxs::gwbufptr_to_gwbuf(protocol_response.release()));
                }
            }
            while (state == State::READY && !m_requests.empty());
        }
    }
    else
    {
        // If the database is not ready, there cannot be a response.
        mxb_assert(!protocol_response);
    }

    return true;
}

void NoSQL::kill_client()
{
    m_context.client_connection().dcb()->session()->kill();
}

State NoSQL::handle_delete(GWBUF* pRequest, packet::Delete&& req, GWBUF** ppResponse)
{
    log_in("Request(Delete)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()), &m_context, &m_config);

    Command::Response response;
    State state = m_sDatabase->handle_delete(pRequest, std::move(req), &response);
    *ppResponse = response.release();

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_insert(GWBUF* pRequest, packet::Insert&& req, GWBUF** ppResponse)
{
    log_in("Request(Insert)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()), &m_context, &m_config);

    Command::Response response;
    State state = m_sDatabase->handle_insert(pRequest, std::move(req), &response);
    *ppResponse = response.release();

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_update(GWBUF* pRequest, packet::Update&& req, GWBUF** ppResponse)
{
    log_in("Request(Update)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()), &m_context, &m_config);

    Command::Response response;
    State state = m_sDatabase->handle_update(pRequest, std::move(req), &response);
    *ppResponse = response.release();

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_query(GWBUF* pRequest, packet::Query&& req, GWBUF** ppResponse)
{
    log_in("Request(Query)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()), &m_context, &m_config);

    Command::Response response;
    State state = m_sDatabase->handle_query(pRequest, std::move(req), &response);
    *ppResponse = response.release();

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_get_more(GWBUF* pRequest, packet::GetMore&& req, GWBUF** ppResponse)
{
    log_in("Request(GetMore)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()), &m_context, &m_config);

    Command::Response response;
    State state = m_sDatabase->handle_get_more(pRequest, std::move(req), &response);
    *ppResponse = response.release();

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, GWBUF** ppResponse)
{
    log_in("Request(KillCursors)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create("admin", &m_context, &m_config);

    Command::Response response;
    State state = m_sDatabase->handle_kill_cursors(pRequest, std::move(req), &response);
    *ppResponse = response.release();

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_msg(GWBUF* pRequest, packet::Msg&& req, GWBUF** ppResponse)
{
    log_in("Request(Msg)", req);

    State state = State::READY;

    const auto& doc = req.document();

    auto element = doc["$db"];

    if (element)
    {
        if (element.type() == bsoncxx::type::k_utf8)
        {
            auto utf8 = element.get_utf8();

            string name(utf8.value.data(), utf8.value.size());

            mxb_assert(!m_sDatabase.get());
            m_sDatabase = Database::create(name, &m_context, &m_config);

            Command::Response response;
            state = m_sDatabase->handle_msg(pRequest, std::move(req), &response);
            *ppResponse = response.release();

            if (state == State::READY)
            {
                m_sDatabase.reset();
            }
        }
        else
        {
            MXB_ERROR("Closing client connection; key '$db' found, but value is not utf8.");
            kill_client();
        }
    }
    else
    {
        MXB_ERROR("Closing client connection; document did not "
                  "contain the expected key '$db': %s",
                  req.to_string().c_str());
        kill_client();
    }

    return state;
}

}
