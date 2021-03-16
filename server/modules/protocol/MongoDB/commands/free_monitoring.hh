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
// https://docs.mongodb.com/manual/reference/command/nav-free-monitoring/
//

#include "defs.hh"

namespace mxsmongo
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/getFreeMonitoringStatus/
class GetFreeMonitoringStatus : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        bsoncxx::builder::basic::document builder;

        builder.append(bsoncxx::builder::basic::kvp("state", "undecided"));
        builder.append(bsoncxx::builder::basic::kvp("ok", 1));

        return create_response(builder.extract());
    }
};


// https://docs.mongodb.com/manual/reference/command/setFreeMonitoring/


}

}
