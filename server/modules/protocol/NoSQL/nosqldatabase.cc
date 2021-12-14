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

#include "nosqldatabase.hh"
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <bsoncxx/exception/exception.hpp>
#include "config.hh"
#include "nosqlcommands.hh"

using namespace std;

namespace nosql
{

Database::Database(const std::string& name, NoSQL::Context* pContext, Config* pConfig)
    : m_name(name)
    , m_context(*pContext)
    , m_config(*pConfig)
{
}

Database::~Database()
{
}

//static
unique_ptr<Database> Database::create(const std::string& name, NoSQL::Context* pContext, Config* pConfig)
{
    return unique_ptr<Database>(new Database(name, pContext, pConfig));
}

State Database::handle_delete(GWBUF* pRequest, packet::Delete&& packet, GWBUF** ppResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpDeleteCommand(this, pRequest, std::move(packet)));

    return execute_command(std::move(sCommand), ppResponse);
}

State Database::handle_insert(GWBUF* pRequest, packet::Insert&& req, GWBUF** ppResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpInsertCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), ppResponse);
}

State Database::handle_query(GWBUF* pRequest, packet::Query&& req, GWBUF** ppResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpQueryCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), ppResponse);
}

State Database::handle_update(GWBUF* pRequest, packet::Update&& req, GWBUF** ppResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpUpdateCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), ppResponse);
}

State Database::handle_get_more(GWBUF* pRequest, packet::GetMore&& packet, GWBUF** ppResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpGetMoreCommand(this, pRequest, std::move(packet)));

    return execute_command(std::move(sCommand), ppResponse);
}

State Database::handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, GWBUF** ppResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpKillCursorsCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), ppResponse);
}

State Database::handle_msg(GWBUF* pRequest, packet::Msg&& req, GWBUF** ppResponse)
{
    mxb_assert(is_ready());

    State state = State::READY;
    GWBUF* pResponse = nullptr;

    auto sCommand = OpMsgCommand::get(this, pRequest, std::move(req));

    if (sCommand->is_admin() && m_name != "admin")
    {
        SoftError error(sCommand->name() + " may only be run against the admin database.",
                        error::UNAUTHORIZED);
        m_context.set_last_error(error.create_last_error());

        pResponse = error.create_response(*sCommand.get());
    }
    else if (!sCommand->is_get_last_error())
    {
        m_context.reset_error();
    }

    if (!pResponse)
    {
        state = execute_command(std::move(sCommand), &pResponse);
    }

    *ppResponse = pResponse;
    return state;
}

GWBUF* Database::translate(mxs::Buffer&& mariadb_response)
{
    mxb_assert(is_busy());
    mxb_assert(m_sCommand.get());

    State state = State::READY;
    GWBUF* pResponse = nullptr;

    try
    {
        state = m_sCommand->translate(std::move(mariadb_response), &pResponse);
    }
    catch (const Exception& x)
    {
        m_context.set_last_error(x.create_last_error());

        if (!m_sCommand->is_silent())
        {
            pResponse = x.create_response(*m_sCommand.get());
        }
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), error::COMMAND_FAILED);
        m_context.set_last_error(error.create_last_error());

        if (!m_sCommand->is_silent())
        {
            pResponse = error.create_response(*m_sCommand);
        }
    }

    if (state == State::READY)
    {
        m_sCommand.reset();
        set_ready();
    }

    return pResponse;
}

State Database::execute_command(std::unique_ptr<Command> sCommand, GWBUF** ppResponse)
{
    State state = State::READY;
    GWBUF* pResponse = nullptr;

    try
    {
        m_sCommand = std::move(sCommand);
        set_busy();

        // This check could be made earlier, but it is more convenient to do it here.
        if (m_name.empty() || m_name.find_first_of(" /\\.\"$") != string::npos)
        {
            ostringstream ss;
            ss << "Invalid database name: '" << m_name << "'";

            throw SoftError(ss.str(), error::INVALID_NAMESPACE);
        }

        state = m_sCommand->execute(&pResponse);
    }
    catch (const Exception& x)
    {
        const char* zMessage = x.what();

        // If there is no message, the error was 1) stored in the returned 'writeErrors'
        // array and 2) already warned for.
        if (*zMessage != 0)
        {
            MXB_WARNING("nosql exception occurred when executing NoSQL command: %s", zMessage);
        }

        m_context.set_last_error(x.create_last_error());

        if (!m_sCommand->is_silent())
        {
            pResponse = x.create_response(*m_sCommand.get());
        }
    }
    catch (const bsoncxx::exception& x)
    {
        MXB_ERROR("bsoncxx exeception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), error::FAILED_TO_PARSE);
        m_context.set_last_error(error.create_last_error());

        if (!m_sCommand->is_silent())
        {
            pResponse = error.create_response(*m_sCommand);
        }
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), error::FAILED_TO_PARSE);
        m_context.set_last_error(error.create_last_error());

        if (!m_sCommand->is_silent())
        {
            pResponse = error.create_response(*m_sCommand);
        }
    }

    if (state == State::READY)
    {
        m_sCommand.reset();
        set_ready();
    }

    *ppResponse = pResponse;
    return state;
}

}
