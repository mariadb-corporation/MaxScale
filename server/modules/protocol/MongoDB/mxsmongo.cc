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
#include "mxsmongodatabase.hh"

using namespace std;

namespace
{

struct ThisUnit
{
    const map<const char*, mxsmongo::Command> commands_by_key =
    {
        {
            mxsmongo::keys::ISMASTER,  mxsmongo::Command::ISMASTER
        }
    };
} this_unit;

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

mxsmongo::Mongo::Mongo()
{
}

mxsmongo::Mongo::~Mongo()
{
}

GWBUF* mxsmongo::Mongo::handle_request(const mxsmongo::Packet& req, mxs::Component& downstream)
{
    GWBUF* pResponse = nullptr;

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
        pResponse = handle_msg(mxsmongo::Msg(req), downstream);
        break;

    case MONGOC_OPCODE_QUERY:
        pResponse = handle_query(mxsmongo::Query(req), downstream);
        break;

    default:
        MXS_ERROR("Unknown opcode %d.", req.opcode());
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* mxsmongo::Mongo::translate(GWBUF* pResponse)
{
    mxb_assert(m_sDatabase.get());

    pResponse = m_sDatabase->translate(pResponse);

    m_sDatabase.reset();

    // TODO: In case multiple Mongo requests have been queued up, this is the place where
    // TODO: a pending one should be handled.

    return pResponse;
}

GWBUF* mxsmongo::Mongo::handle_query(const mxsmongo::Query& req, mxs::Component& downstream)
{
    MXS_NOTICE("\n%s\n", req.to_string().c_str());

    auto sDatabase = Database::create(req.collection(), &m_context);

    GWBUF* pResponse = sDatabase->handle_query(req, downstream);

    if (!pResponse)
    {
        // TODO: Not all Mongo request will have an response. E.g an insert will not have,
        // TODO: but the MariaDB insert we translate it into, obviously will have. So, we
        // TODO: need to prepare for the case that the Mongo client just keeps on sending
        // TODO: inserts, without waiting for them to be executed.
        m_sDatabase = std::move(sDatabase);
    }

    return pResponse;
}

GWBUF* mxsmongo::Mongo::handle_msg(const mxsmongo::Msg& req, mxs::Component& downstream)
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

                pResponse = sDatabase->handle_command(req, doc, downstream);

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
