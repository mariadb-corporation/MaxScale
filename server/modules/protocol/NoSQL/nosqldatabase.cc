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

#include "nosqldatabase.hh"
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <bsoncxx/exception/exception.hpp>
#include "config.hh"

using namespace std;

nosql::Database::Database(const std::string& name,
                          NoSQL::Context* pContext,
                          Config* pConfig)
    : m_name(name)
    , m_context(*pContext)
    , m_config(*pConfig)
{
}

nosql::Database::~Database()
{
}

//static
unique_ptr<nosql::Database> nosql::Database::create(const std::string& name,
                                                    NoSQL::Context* pContext,
                                                    Config* pConfig)
{
    return unique_ptr<Database>(new Database(name, pContext, pConfig));
}

GWBUF* nosql::Database::handle_query(GWBUF* pRequest, const nosql::Query& req)
{
    mxb_assert(is_ready());

    Command::DocumentArguments arguments;

    return execute(pRequest, req, req.query(), arguments);
}

GWBUF* nosql::Database::handle_command(GWBUF* pRequest,
                                       const nosql::Msg& req,
                                       const bsoncxx::document::view& doc)
{
    mxb_assert(is_ready());

    return execute(pRequest, req, doc, req.arguments());
}

GWBUF* nosql::Database::translate(mxs::Buffer&& mariadb_response)
{
    mxb_assert(is_pending());
    mxb_assert(m_sCommand.get());

    GWBUF* pResponse = nullptr;

    Command::State state = Command::READY;

    try
    {
        state = m_sCommand->translate(std::move(mariadb_response), &pResponse);
    }
    catch (const nosql::Exception& x)
    {
        m_context.set_last_error(x.create_last_error());

        pResponse = x.create_response(*m_sCommand.get());
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), nosql::error::COMMAND_FAILED);
        m_context.set_last_error(error.create_last_error());

        pResponse = error.create_response(*m_sCommand);
    }

    if (state == Command::READY)
    {
        mxb_assert(state == Command::READY);

        m_sCommand.reset();

        set_ready();
    }

    return pResponse;
}

GWBUF* nosql::Database::execute(std::unique_ptr<Command> sCommand)
{
    GWBUF* pResponse = nullptr;

    try
    {
        if (sCommand->is_admin() && m_name != "admin")
        {
            throw SoftError(sCommand->name() + " may only be run against the admin database.",
                            error::UNAUTHORIZED);
        }

        if (sCommand->name() != key::GETLASTERROR)
        {
            m_context.reset_error();
        }

        pResponse = sCommand->execute();
    }
    catch (const nosql::Exception& x)
    {
        m_context.set_last_error(x.create_last_error());

        pResponse = x.create_response(*sCommand.get());
    }
    catch (const bsoncxx::exception& x)
    {
        MXB_ERROR("bsoncxx exeception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), nosql::error::FAILED_TO_PARSE);
        m_context.set_last_error(error.create_last_error());

        pResponse = error.create_response(*sCommand);
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), nosql::error::FAILED_TO_PARSE);
        m_context.set_last_error(error.create_last_error());

        pResponse = error.create_response(*sCommand);
    }

    if (!pResponse)
    {
        m_sCommand = std::move(sCommand);
        set_pending();
    }

    return pResponse;
}
