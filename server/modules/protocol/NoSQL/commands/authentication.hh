/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

//
// https://docs.mongodb.com/v4.4/reference/command/nav-authentication/
//

#include "defs.hh"
#include "../clientconnection.hh"

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

    enum class Approach
    {
        UNCONDITIONAL, // Always.
        CONDITIONAL    // Only if current database is the authentication database.
    };

    void populate_response(DocumentBuilder& doc) override
    {
        logout(m_database, Approach::CONDITIONAL);

        doc.append(kvp(key::OK, 1));
    }

    static void logout(Database& database, Approach approach)
    {
        auto& context = database.context();

        if (context.authenticated()
            && (approach == Approach::UNCONDITIONAL || context.authentication_db() == database.name()))
        {
            auto& session = context.session();

            if (session.is_started())
            {
                // This could (in some cases) be handled as a COM_CHANGE_USER,
                // but simpler to just close the session as that will cause the
                // backend connections to be closed and a reauthentication when
                // needed.
                session.close();
            }

            auto& config = database.config();

            config.user = config.config_user;
            config.password = config.config_password;

            context.set_unauthenticated();
            context.client_connection().setup_session(config.user, config.password);
        }
    }
};

}

}
