/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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

State NoSQL::handle_request(GWBUF* pRequest)
{
    State state = State::READY;

    if (!m_sDatabase)
    {
        m_pCurrent_request = pRequest;

        try
        {
            // If no database operation is in progress, we proceed.
            packet::Packet req(pRequest);

            mxb_assert(req.msg_len() == (int)pRequest->length());

            Command::Response response;

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
                state = handle_get_more(pRequest, packet::GetMore(req), &response);
                break;

            case MONGOC_OPCODE_KILL_CURSORS:
                state = handle_kill_cursors(pRequest, packet::KillCursors(req), &response);
                break;

            case MONGOC_OPCODE_DELETE:
                state = handle_delete(pRequest, packet::Delete(req), &response);
                break;

            case MONGOC_OPCODE_INSERT:
                state = handle_insert(pRequest, packet::Insert(req), &response);
                break;

            case MONGOC_OPCODE_MSG:
                state = handle_msg(pRequest, packet::Msg(req), &response);
                break;

            case MONGOC_OPCODE_QUERY:
                state = handle_query(pRequest, packet::Query(req), &response);
                break;

            case MONGOC_OPCODE_UPDATE:
                state = handle_update(pRequest, packet::Update(req), &response);
                break;

            default:
                {
                    mxb_assert(!true);
                    ostringstream ss;
                    ss << "Unknown packet " << req.opcode() << " received.";
                    throw std::runtime_error(ss.str());
                }
            }

            if (response)
            {
                // If we got the response immediately, it can not have been a SELECT
                // that was sent to the backend; hence there cannot be invalidation words.
                flush_response(response);
            }
        }
        catch (const std::exception& x)
        {
            MXB_ERROR("Closing client connection: %s", x.what());
            kill_client();
        }

        m_pCurrent_request = nullptr;

        delete pRequest;
    }
    else
    {
        // Otherwise we push it on the request queue.
        m_requests.push_back(pRequest);
    }

    return state;
}

bool NoSQL::clientReply(GWBUF&& mariadb_response, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(m_pDcb);
    mxb_assert(m_sDatabase.get());

    Command::Response response = m_sDatabase->translate(std::move(mariadb_response));

    if (m_sDatabase->is_ready())
    {
        m_sDatabase.reset();

        if (response)
        {
            flush_response(response);
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

                state = handle_request(pRequest);
            }
            while (state == State::READY && !m_requests.empty());
        }
    }
    else
    {
        // If the database is not ready, there cannot be a response.
        mxb_assert(!response);
    }

    return true;
}

void NoSQL::kill_client()
{
    m_context.client_connection().dcb()->session()->kill();
}

State NoSQL::handle_delete(GWBUF* pRequest, packet::Delete&& req, Command::Response* pResponse)
{
    log_in("Request(Delete)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()),
                                   &m_context, &m_config, m_pCache_filter_session);

    State state = m_sDatabase->handle_delete(pRequest, std::move(req), pResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_insert(GWBUF* pRequest, packet::Insert&& req, Command::Response* pResponse)
{
    log_in("Request(Insert)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()),
                                   &m_context, &m_config, m_pCache_filter_session);

    State state = m_sDatabase->handle_insert(pRequest, std::move(req), pResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_update(GWBUF* pRequest, packet::Update&& req, Command::Response* pResponse)
{
    log_in("Request(Update)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()),
                                   &m_context, &m_config, m_pCache_filter_session);

    State state = m_sDatabase->handle_update(pRequest, std::move(req), pResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_query(GWBUF* pRequest, packet::Query&& req, Command::Response* pResponse)
{
    log_in("Request(Query)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()),
                                   &m_context, &m_config, m_pCache_filter_session);

    State state = m_sDatabase->handle_query(pRequest, std::move(req), pResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_get_more(GWBUF* pRequest, packet::GetMore&& req, Command::Response* pResponse)
{
    log_in("Request(GetMore)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create(extract_database(req.collection()),
                                   &m_context, &m_config, m_pCache_filter_session);

    State state = m_sDatabase->handle_get_more(pRequest, std::move(req), pResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, Command::Response* pResponse)
{
    log_in("Request(KillCursors)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = Database::create("admin", &m_context, &m_config, m_pCache_filter_session);

    State state = m_sDatabase->handle_kill_cursors(pRequest, std::move(req), pResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_msg(GWBUF* pRequest, packet::Msg&& req, Command::Response* pResponse)
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
            m_sDatabase = Database::create(name, &m_context, &m_config, m_pCache_filter_session);

            state = m_sDatabase->handle_msg(pRequest, std::move(req), pResponse);

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

void NoSQL::flush_response(Command::Response& response)
{
    mxb_assert(response);

    if (m_pCache_filter_session && response.is_cacheable())
    {
        Command* pCommand = response.command();
        mxb_assert(pCommand);

        auto table = response.command()->table(Command::Quoted::NO);
        vector<string> invalidation_words { table };

        auto& user = m_pCache_filter_session->user();
        auto& host = m_pCache_filter_session->host();
        auto* zDefault_db = m_pCache_filter_session->default_db();

        const CacheKey& key = pCommand->cache_key();
        mxb_assert(key);

        const auto& config = m_pCache_filter_session->config();

        if (config.debug & CACHE_DEBUG_DECISIONS)
        {
            MXB_NOTICE("Storing NoSQL response, invalidated by changes in: '%s'", table.c_str());
        }

        auto rv = m_pCache_filter_session->put_value(key, invalidation_words, *response.get(), nullptr);

        mxb_assert(!CACHE_RESULT_IS_PENDING(rv));
        mxb_assert(CACHE_RESULT_IS_OK(rv) || CACHE_RESULT_IS_OUT_OF_RESOURCES(rv));
    }

    m_pDcb->writeq_append(nosql::gwbufptr_to_gwbuf(response.release()));
}

}
