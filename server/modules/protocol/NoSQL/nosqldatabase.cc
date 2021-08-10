/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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
#include "commands/query_and_write_operation.hh"

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

GWBUF* nosql::Database::handle_delete(GWBUF* pRequest, nosql::Delete&& packet)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new nosql::OpDeleteCommand(this, pRequest, std::move(packet)));

    return execute_command(std::move(sCommand));
}

GWBUF* nosql::Database::handle_insert(GWBUF* pRequest, nosql::Insert&& req)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new nosql::OpInsertCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand));
}

GWBUF* nosql::Database::handle_query(GWBUF* pRequest, const nosql::Query& req)
{
    mxb_assert(is_ready());

    OpMsgCommand::DocumentArguments arguments;

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

GWBUF* nosql::Database::execute_msg_command(std::unique_ptr<OpMsgCommand> sCommand)
{
    GWBUF* pResponse = nullptr;

    if (sCommand->is_admin() && m_name != "admin")
    {
        SoftError error(sCommand->name() + " may only be run against the admin database.",
                        error::UNAUTHORIZED);
        m_context.set_last_error(error.create_last_error());

        pResponse = error.create_response(*sCommand.get());
    }
    else if (sCommand->name() != command::GetLastError::KEY)
    {
        m_context.reset_error();
    }

    if (!pResponse)
    {
        pResponse = execute_command(std::move(sCommand));
    }

    return pResponse;
}

GWBUF* nosql::Database::execute_command(std::unique_ptr<Command> sCommand)
{
    GWBUF* pResponse = nullptr;

    try
    {
        m_sCommand = std::move(sCommand);
        set_pending();

        pResponse = m_sCommand->execute();
    }
    catch (const nosql::Exception& x)
    {
        m_context.set_last_error(x.create_last_error());

        pResponse = x.create_response(*m_sCommand.get());
    }
    catch (const bsoncxx::exception& x)
    {
        MXB_ERROR("bsoncxx exeception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), nosql::error::FAILED_TO_PARSE);
        m_context.set_last_error(error.create_last_error());

        pResponse = error.create_response(*m_sCommand);
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), nosql::error::FAILED_TO_PARSE);
        m_context.set_last_error(error.create_last_error());

        pResponse = error.create_response(*m_sCommand);
    }

    if (pResponse)
    {
        set_ready();
    }

    return pResponse;
}
