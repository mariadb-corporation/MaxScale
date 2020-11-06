/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "mxsmongo.hh"
#include <sstream>
#include <map>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxscale/dcb.hh>
#include "mxsmongodatabase.hh"

using namespace std;

namespace
{

struct ThisUnit
{
    const map<const char*, mxsmongo::Command> commands_by_key =
    {
        { mxsmongo::keys::FIND,      mxsmongo::Command::FIND },
        { mxsmongo::keys::ISMASTER,  mxsmongo::Command::ISMASTER }
    };

    bool continue_on_unknown = getenv("MONGODBCLIENT_CONTINUE_ON_UNKNOWN") != nullptr;
} this_unit;

}

bool mxsmongo::continue_on_unknown()
{
    return this_unit.continue_on_unknown;
}

const char* mxsmongo::opcode_to_string(int code)
{
    switch (code)
    {
    case MONGOC_OPCODE_REPLY:
        return "MONGOC_OPCODE_REPLY";

    case MONGOC_OPCODE_UPDATE:
        return "MONGOC_OPCODE_UPDATE";

    case MONGOC_OPCODE_INSERT:
        return "MONGOC_OPCODE_INSERT";

    case MONGOC_OPCODE_QUERY:
        return "MONGOC_OPCODE_QUERY";

    case MONGOC_OPCODE_GET_MORE:
        return "OPCODE_GET_MORE";

    case MONGOC_OPCODE_DELETE:
        return "MONGOC_OPCODE_DELETE";

    case MONGOC_OPCODE_KILL_CURSORS:
        return "OPCODE_KILL_CURSORS";

    case MONGOC_OPCODE_COMPRESSED:
        return "MONGOC_OPCODE_COMPRESSED";

    case MONGOC_OPCODE_MSG:
        return "MONGOC_OPCODE_MSG";

    default:
        mxb_assert(!true);
        return "MONGOC_OPCODE_UKNOWN";
    }
}

mxsmongo::Command mxsmongo::get_command(const bsoncxx::document::view& doc)
{
    mxsmongo::Command command = mxsmongo::Command::UNKNOWN;

    // TODO: At some point it might be good to apply some kind of heuristic for
    // TODO: deciding whether to loop over the keys of the document or over
    // TODO: the keys in the map. Or, can we be certain that e.g. the first
    // TODO: field in the document is the command?

    for (const auto& kv : this_unit.commands_by_key)
    {
        if (doc.find(kv.first) != doc.cend())
        {
            command = kv.second;
            break;
        }
    }

    return command;
}

mxsmongo::Mongo::Mongo(mxs::Component* pDownstream)
    : m_context(pDownstream)
{
}

mxsmongo::Mongo::~Mongo()
{
}

GWBUF* mxsmongo::Mongo::handle_request(GWBUF* pRequest)
{
    GWBUF* pResponse = nullptr;

    if (!m_sDatabase)
    {
        // If no database operation is in progress, we proceed.
        mxsmongo::Packet req(pRequest);

        mxb_assert(req.msg_len() == (int)gwbuf_length(pRequest));

        switch (req.opcode())
        {
        case MONGOC_OPCODE_COMPRESSED:
        case MONGOC_OPCODE_DELETE:
        case MONGOC_OPCODE_GET_MORE:
        case MONGOC_OPCODE_INSERT:
        case MONGOC_OPCODE_KILL_CURSORS:
        case MONGOC_OPCODE_REPLY:
        case MONGOC_OPCODE_UPDATE:
            MXS_ERROR("Packet %s not handled (yet).", mxsmongo::opcode_to_string(req.opcode()));
            mxb_assert(!true);
            break;

        case MONGOC_OPCODE_MSG:
            pResponse = handle_msg(pRequest, mxsmongo::Msg(req));
            break;

        case MONGOC_OPCODE_QUERY:
            pResponse = handle_query(pRequest, mxsmongo::Query(req));
            break;

        default:
            MXS_ERROR("Unknown opcode %d.", req.opcode());
            mxb_assert(!true);
        }

        gwbuf_free(pRequest);
    }
    else
    {
        // Otherwise we push it on the request queue.
        m_requests.push_back(pRequest);
    }

    return pResponse;
}

int32_t mxsmongo::Mongo::clientReply(GWBUF* pMariaDB_response, DCB* pDcb)
{
    mxb_assert(m_sDatabase.get());

    GWBUF* pMongoDB_response = m_sDatabase->translate(*pMariaDB_response);
    gwbuf_free(pMariaDB_response);

    m_sDatabase.reset();

    if (pMongoDB_response)
    {
        pDcb->writeq_append(pMongoDB_response);
    }

    if (!m_requests.empty())
    {
        // Loop as long as responses to requests can be generated immediately.
        // If it can't then we'll continue once clientReply() is called anew.
        do
        {
            mxb_assert(!m_sDatabase.get());

            GWBUF* pRequest = m_requests.front();
            m_requests.pop_front();

            pMongoDB_response = handle_request(pRequest);

            if (pMongoDB_response)
            {
                // The response could be generated immediately, just send it.
                pDcb->writeq_append(pMongoDB_response);
            }
        }
        while (pMongoDB_response && !m_requests.empty());
    }

    return 0;
}

GWBUF* mxsmongo::Mongo::handle_query(GWBUF* pRequest, const mxsmongo::Query& req)
{
    MXS_NOTICE("\n%s\n", req.to_string().c_str());

    auto sDatabase = Database::create(req.collection(), &m_context);

    GWBUF* pResponse = sDatabase->handle_query(pRequest, req);

    if (!pResponse)
    {
        mxb_assert(!m_sDatabase.get());
        m_sDatabase = std::move(sDatabase);
    }

    return pResponse;
}

GWBUF* mxsmongo::Mongo::handle_msg(GWBUF* pRequest, const mxsmongo::Msg& req)
{
    MXS_NOTICE("\n%s\n", req.to_string().c_str());

    mxb_assert(req.documents().size() == 1); // TODO

    GWBUF* pResponse = nullptr;

    for (const auto& doc : req.documents())
    {
        auto element = doc["$db"];

        if (element)
        {
            if (element.type() == bsoncxx::type::k_utf8)
            {
                auto utf8 = element.get_utf8();

                string name(utf8.value.data(), utf8.value.size());
                auto sDatabase = Database::create(name, &m_context);

                pResponse = sDatabase->handle_command(pRequest, req, doc);

                if (!pResponse)
                {
                    // TODO: See handle_query()
                    m_sDatabase = std::move(sDatabase);
                }
            }
            else
            {
                MXS_ERROR("Key '$db' found, but value is not utf8.");
                mxb_assert(!true);
            }
        }
        else
        {
            MXS_ERROR("Document did not contain the expected key '$db': %s",
                      req.to_string().c_str());
            mxb_assert(!true);
        }
    }

    return pResponse;
}
