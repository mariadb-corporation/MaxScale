/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-replication/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/applyOps/

// https://docs.mongodb.com/v4.4/reference/command/isMaster/
class IsMaster final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "isMaster";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        populate_response(m_database, doc);
    }

    static void populate_response(const Database& database, DocumentBuilder& doc)
    {
        doc.append(kvp(key::ISMASTER, true));
        doc.append(kvp(key::TOPOLOGY_VERSION, topology_version()));
        doc.append(kvp(key::MAX_BSON_OBJECT_SIZE, protocol::MAX_BSON_OBJECT_SIZE));
        doc.append(kvp(key::MAX_MESSAGE_SIZE_BYTES, protocol::MAX_MSG_SIZE));
        doc.append(kvp(key::MAX_WRITE_BATCH_SIZE, protocol::MAX_WRITE_BATCH_SIZE));
        doc.append(kvp(key::LOCALTIME, bsoncxx::types::b_date(std::chrono::system_clock::now())));
        doc.append(kvp(key::LOGICAL_SESSION_TIMEOUT_MINUTES, 30));
        doc.append(kvp(key::CONNECTION_ID, database.context().connection_id()));
        doc.append(kvp(key::MIN_WIRE_VERSION, MIN_WIRE_VERSION));
        doc.append(kvp(key::MAX_WIRE_VERSION, MAX_WIRE_VERSION));
        doc.append(kvp(key::READ_ONLY, false));
        doc.append(kvp(key::OK, 1));
    }
};


// https://docs.mongodb.com/v4.4/reference/command/replSetAbortPrimaryCatchUp/

// https://docs.mongodb.com/v4.4/reference/command/replSetFreeze/

// https://docs.mongodb.com/v4.4/reference/command/replSetGetConfig/

// https://docs.mongodb.com/v4.4/reference/command/replSetGetStatus/
class ReplSetGetStatus final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "replSetGetStatus";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        throw SoftError("not running with --replSet", error::NO_REPLICATION_ENABLED);
    }
};


// https://docs.mongodb.com/v4.4/reference/command/replSetInitiate/

// https://docs.mongodb.com/v4.4/reference/command/replSetMaintenance/

// https://docs.mongodb.com/v4.4/reference/command/replSetReconfig/

// https://docs.mongodb.com/v4.4/reference/command/replSetResizeOplog/

// https://docs.mongodb.com/v4.4/reference/command/replSetStepDown/

// https://docs.mongodb.com/v4.4/reference/command/replSetSyncFrom/


}

}
