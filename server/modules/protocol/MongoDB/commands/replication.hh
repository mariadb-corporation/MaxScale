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

namespace mxsmongo
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/applyOps/

// https://docs.mongodb.com/manual/reference/command/isMaster/
class IsMaster : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        // TODO: Do not simply return a hardwired response.

        auto builder = bsoncxx::builder::stream::document{};
        bsoncxx::document::value doc_value = builder
            << "isMaster" << true
            << "topologyVersion" << mxsmongo::topology_version()
            << "maxBsonObjectSize" << (int32_t)16777216
            << "maxMessageSizeBytes" << (int32_t)48000000
            << "maxWriteBatchSize" << (int32_t)100000
            << "localTime" << bsoncxx::types::b_date(std::chrono::system_clock::now())
            << "logicalSessionTimeoutMinutes" << (int32_t)30
            << "connectionId" << (int32_t)4
            << "minWireVersion" << (int32_t)0
            << "maxWireVersion" << (int32_t)9
            << "readOnly" << false
            << "ok" << (double)1
            << bsoncxx::builder::stream::finalize;

        return create_response(doc_value);
    }
};


// https://docs.mongodb.com/manual/reference/command/replSetAbortPrimaryCatchUp/

// https://docs.mongodb.com/manual/reference/command/replSetFreeze/

// https://docs.mongodb.com/manual/reference/command/replSetGetConfig/

// https://docs.mongodb.com/manual/reference/command/replSetGetStatus/

// https://docs.mongodb.com/manual/reference/command/replSetInitiate/

// https://docs.mongodb.com/manual/reference/command/replSetMaintenance/

// https://docs.mongodb.com/manual/reference/command/replSetReconfig/

// https://docs.mongodb.com/manual/reference/command/replSetResizeOplog/

// https://docs.mongodb.com/manual/reference/command/replSetStepDown/

// https://docs.mongodb.com/manual/reference/command/replSetSyncFrom/


}

}
