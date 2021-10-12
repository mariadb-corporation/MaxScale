/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/manual/reference/command/nav-sessions/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/abortTransaction/

// https://docs.mongodb.com/manual/reference/command/commitTransaction/

// https://docs.mongodb.com/manual/reference/command/endSessions/
class EndSessions final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "endSessions";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/manual/reference/command/killAllSessions/

// https://docs.mongodb.com/manual/reference/command/killAllSessionsByPattern/

// https://docs.mongodb.com/manual/reference/command/killSessions/

// https://docs.mongodb.com/manual/reference/command/refreshSessions/

// https://docs.mongodb.com/manual/reference/command/startSession/


}

}
