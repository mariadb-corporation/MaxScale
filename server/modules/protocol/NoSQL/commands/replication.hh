/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/manual/reference/command/nav-replication/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/applyOps/

// https://docs.mongodb.com/manual/reference/command/isMaster/
class IsMaster final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "isMaster";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        doc.append(kvp("ismaster", true));
        doc.append(kvp("topologyVersion", topology_version()));
        doc.append(kvp("maxBsonObjectSize", protocol::MAX_BSON_OBJECT_SIZE));
        doc.append(kvp("maxMessageSizeBytes", protocol::MAX_MSG_SIZE));
        doc.append(kvp("maxWriteBatchSize", protocol::MAX_WRITE_BATCH_SIZE));
        doc.append(kvp("localTime", bsoncxx::types::b_date(std::chrono::system_clock::now())));
        doc.append(kvp("logicalSessionTimeoutMinutes", 30));
        doc.append(kvp("connectionId", m_database.context().connection_id()));
        doc.append(kvp("minWireVersion", MIN_WIRE_VERSION));
        doc.append(kvp("maxWireVersion", MAX_WIRE_VERSION));
        doc.append(kvp("readOnly", false));
        doc.append(kvp("ok", 1));
    }
};


// https://docs.mongodb.com/manual/reference/command/replSetAbortPrimaryCatchUp/

// https://docs.mongodb.com/manual/reference/command/replSetFreeze/

// https://docs.mongodb.com/manual/reference/command/replSetGetConfig/

// https://docs.mongodb.com/manual/reference/command/replSetGetStatus/
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


// https://docs.mongodb.com/manual/reference/command/replSetInitiate/

// https://docs.mongodb.com/manual/reference/command/replSetMaintenance/

// https://docs.mongodb.com/manual/reference/command/replSetReconfig/

// https://docs.mongodb.com/manual/reference/command/replSetResizeOplog/

// https://docs.mongodb.com/manual/reference/command/replSetStepDown/

// https://docs.mongodb.com/manual/reference/command/replSetSyncFrom/


}

}
