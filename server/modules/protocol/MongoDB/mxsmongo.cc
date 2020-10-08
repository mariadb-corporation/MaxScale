/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mxsmongo.hh"
#include <sstream>
#include <map>

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
