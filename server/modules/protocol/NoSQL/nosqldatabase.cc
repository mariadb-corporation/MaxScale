/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqldatabase.hh"
#include <maxscale/protocol/mariadb/mysql.hh>
#include <bsoncxx/exception/exception.hpp>
#include "clientconnection.hh"
#include "nosqlcommands.hh"
#include "nosqlconfig.hh"

using namespace std;

namespace nosql
{

Database::Database(const std::string& name, Context* pContext, Config* pConfig)
    : m_name(name)
    , m_context(*pContext)
    , m_config(*pConfig)
{
}

Database::~Database()
{
}

//static
unique_ptr<Database> Database::create(const std::string& name, Context* pContext, Config* pConfig)
{
    return unique_ptr<Database>(new Database(name, pContext, pConfig));
}

State Database::handle_delete(GWBUF* pRequest, packet::Delete&& packet, Command::Response* pResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpDeleteCommand(this, pRequest, std::move(packet)));

    return execute_command(std::move(sCommand), pResponse);
}

State Database::handle_insert(GWBUF* pRequest, packet::Insert&& req, Command::Response* pResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpInsertCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), pResponse);
}

State Database::handle_query(GWBUF* pRequest, packet::Query&& req, Command::Response* pResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpQueryCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), pResponse);
}

State Database::handle_update(GWBUF* pRequest, packet::Update&& req, Command::Response* pResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpUpdateCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), pResponse);
}

State Database::handle_get_more(GWBUF* pRequest, packet::GetMore&& packet, Command::Response* pResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpGetMoreCommand(this, pRequest, std::move(packet)));

    return execute_command(std::move(sCommand), pResponse);
}

State Database::handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, Command::Response* pResponse)
{
    mxb_assert(is_ready());

    unique_ptr<Command> sCommand(new OpKillCursorsCommand(this, pRequest, std::move(req)));

    return execute_command(std::move(sCommand), pResponse);
}

State Database::handle_msg(GWBUF* pRequest, packet::Msg&& req, Command::Response* pResponse)
{
    mxb_assert(is_ready());

    State state = State::READY;
    Command::Response response;

    auto sCommand = OpMsgCommand::get(this, pRequest, std::move(req));

    if (sCommand->is_admin() && m_name != "admin")
    {
        SoftError error(sCommand->name() + " may only be run against the admin database.",
                        error::UNAUTHORIZED);
        m_context.set_last_error(error.create_last_error());

        response.reset(error.create_response(*sCommand.get()));
    }
    else if (!sCommand->is_get_last_error())
    {
        m_context.reset_error();
    }

    if (!response)
    {
        state = execute_command(std::move(sCommand), &response);
    }

    *pResponse = std::move(response);
    return state;
}

Command::Response Database::translate(GWBUF&& mariadb_response)
{
    mxb_assert(is_busy());
    mxb_assert(m_sCommand.get());

    State state = State::READY;
    Command::Response response;

    try
    {
        state = m_sCommand->translate(std::move(mariadb_response), &response);
    }
    catch (const Exception& x)
    {
        m_context.set_last_error(x.create_last_error());

        if (!m_sCommand->is_silent())
        {
            response.reset(x.create_response(*m_sCommand.get()));
        }
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), error::COMMAND_FAILED);
        m_context.set_last_error(error.create_last_error());

        if (!m_sCommand->is_silent())
        {
            response.reset(error.create_response(*m_sCommand));
        }
    }

    if (state == State::READY)
    {
        response.set_command(std::move(m_sCommand));
        set_ready();
    }

    return response;
}

State Database::execute_command(std::unique_ptr<Command> sCommand, Command::Response* pResponse)
{
    State state = State::READY;
    Command::Response response;

    bool ready = true;

    auto& session = m_context.session();

    if (sCommand->session_must_be_ready() && !session.is_started())
    {
        ready = session.start();

        if (!ready)
        {
            MXB_ERROR("Could not start session, closing client connection.");
            m_context.session().kill();
        }
    }

    if (ready)
    {
        try
        {
            m_sCommand = std::move(sCommand);
            set_busy();

            // This check could be made earlier, but it is more convenient to do it here.
            if (!is_valid_database_name(m_name))
            {
                ostringstream ss;
                ss << "Invalid database name: '" << m_name << "'";

                throw SoftError(ss.str(), error::INVALID_NAMESPACE);
            }

            if (m_config.should_authenticate())
            {
                m_sCommand->authenticate();
            }

            if (m_config.should_authorize())
            {
                m_sCommand->authorize(m_context.role_mask_of(m_name));
            }

            state = m_sCommand->execute(&response);
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
                response.reset(x.create_response(*m_sCommand.get()));
            }
        }
        catch (const bsoncxx::exception& x)
        {
            MXB_ERROR("bsoncxx exeception occurred when parsing NoSQL command: %s", x.what());

            HardError error(x.what(), error::FAILED_TO_PARSE);
            m_context.set_last_error(error.create_last_error());

            if (!m_sCommand->is_silent())
            {
                response.reset(error.create_response(*m_sCommand));
            }
        }
        catch (const std::exception& x)
        {
            MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

            HardError error(x.what(), error::FAILED_TO_PARSE);
            m_context.set_last_error(error.create_last_error());

            if (!m_sCommand->is_silent())
            {
                response.reset(error.create_response(*m_sCommand));
            }
        }
    }

    if (state == State::READY)
    {
        response.set_command(std::move(m_sCommand));
        set_ready();
    }

    *pResponse = std::move(response);
    return state;
}

}
