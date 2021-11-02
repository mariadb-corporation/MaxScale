/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-user-management/
//
#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/createUser/

// https://docs.mongodb.com/v4.4/reference/command/dropAllUsersFromDatabase/
class DropAllUsersFromDatabase : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "dropAllUsersFromDatabase";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        doc.append(kvp(key::N, 0));
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/dropUser/

// https://docs.mongodb.com/v4.4/reference/command/grantRolesToUser/

// https://docs.mongodb.com/v4.4/reference/command/revokeRolesFromUser/

// https://docs.mongodb.com/v4.4/reference/command/updateUser/

// https://docs.mongodb.com/v4.4/reference/command/usersInfo/


}

}
