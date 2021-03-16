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
// https://docs.mongodb.com/manual/reference/command/nav-diagnostic/
//

#include "defs.hh"

namespace mxsmongo
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/availableQueryOptions/

// https://docs.mongodb.com/manual/reference/command/buildInfo/
class BuildInfo : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        // TODO: Do not simply return a hardwired response.
        bsoncxx::builder::basic::document builder;

        builder.append(bsoncxx::builder::basic::kvp("version", "4.4.1"));

        bsoncxx::builder::basic::array version_builder;

        version_builder.append(4);
        version_builder.append(4);
        version_builder.append(1);

        builder.append(bsoncxx::builder::basic::kvp("versionArray", version_builder.extract()));

        return create_response(builder.extract());
    }
};


// https://docs.mongodb.com/manual/reference/command/collStats/

// https://docs.mongodb.com/manual/reference/command/connPoolStats/

// https://docs.mongodb.com/manual/reference/command/connectionStatus/

// https://docs.mongodb.com/manual/reference/command/cursorInfo/

// https://docs.mongodb.com/manual/reference/command/dataSize/

// https://docs.mongodb.com/manual/reference/command/dbHash/

// https://docs.mongodb.com/manual/reference/command/dbStats/

// https://docs.mongodb.com/manual/reference/command/diagLogging/

// https://docs.mongodb.com/manual/reference/command/driverOIDTest/

// https://docs.mongodb.com/manual/reference/command/explain/

// https://docs.mongodb.com/manual/reference/command/features/

// https://docs.mongodb.com/manual/reference/command/getCmdLineOpts/

// https://docs.mongodb.com/manual/reference/command/getLog/

// https://docs.mongodb.com/manual/reference/command/hostInfo/

// https://docs.mongodb.com/manual/reference/command/isSelf/

// https://docs.mongodb.com/manual/reference/command/listCommands/

// https://docs.mongodb.com/manual/reference/command/lockInfo/

// https://docs.mongodb.com/manual/reference/command/netstat/

// https://docs.mongodb.com/manual/reference/command/ping/

// https://docs.mongodb.com/manual/reference/command/profile/

// https://docs.mongodb.com/manual/reference/command/serverStatus/

// https://docs.mongodb.com/manual/reference/command/shardConnPoolStats/

// https://docs.mongodb.com/manual/reference/command/top/

// https://docs.mongodb.com/manual/reference/command/validate/

// https://docs.mongodb.com/manual/reference/command/whatsmyuri/
class WhatsMyUri : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        bsoncxx::builder::basic::document builder;

        builder.append(bsoncxx::builder::basic::kvp("you", "127.0.0.1:49388"));
        builder.append(bsoncxx::builder::basic::kvp("ok", 1));

        return create_response(builder.extract());
    }
};


}

}
