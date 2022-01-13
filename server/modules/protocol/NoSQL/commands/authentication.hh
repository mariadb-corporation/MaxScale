/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-authentication/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/authenticate/

// https://docs.mongodb.com/v4.4/reference/command/getnonce/

// https://docs.mongodb.com/v4.4/reference/command/logout/
class Logout final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "logout";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        auto& config = m_database.config();

        config.user.clear();
        config.password.clear();

        doc.append(kvp(key::OK, 1));
    }
};

}

}
