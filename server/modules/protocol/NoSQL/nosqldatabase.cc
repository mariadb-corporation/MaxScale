/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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

Database::Database(const std::string& name,
                   Context* pContext,
                   Config* pConfig,
                   CacheFilterSession* pCache_filter_session)
    : m_name(name)
    , m_context(*pContext)
    , m_config(*pConfig)
    , m_pCache_filter_session(pCache_filter_session)
{
}

Database::~Database()
{
}

//static
unique_ptr<Database> Database::create(const std::string& name,
                                      Context* pContext,
                                      Config* pConfig,
                                      CacheFilterSession* pCache_filter_session)
{
    return unique_ptr<Database>(new Database(name, pContext, pConfig, pCache_filter_session));
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

    auto name_and_command = OpMsgCommand::get_info(req.document());

    const auto& name = name_and_command.first;
    const auto& command = name_and_command.second;

    if (command.is_admin && m_name != "admin")
    {
        SoftError error(name + " may only be run against the admin database.", error::UNAUTHORIZED);
        m_context.set_last_error(error.create_last_error());

        // TODO: It ought to be possible to create the reponse from the command info
        // TODO: without instantiating the command, but for later.
        auto sCommand = command.create_default(name, this, pRequest, std::move(req));
        response.reset(error.create_response(*sCommand.get()), Command::Response::Status::NOT_CACHEABLE);
    }
    else
    {
        CacheKey cache_key;
        if (command.is_cacheable && m_pCache_filter_session)
        {
            response = get_cached_response(name, pRequest, req, &cache_key);
        }

        if (!response)
        {
            auto sCommand = command.create_default(name, this, pRequest, std::move(req));

            if (cache_key)
            {
                sCommand->set_cache_key(std::move(cache_key));
            }

            if (!sCommand->is_get_last_error())
            {
                m_context.reset_error();
            }

            state = execute_command(std::move(sCommand), &response);
        }
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
            response.reset(x.create_response(*m_sCommand.get()), Command::Response::Status::NOT_CACHEABLE);
        }
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

        HardError error(x.what(), error::COMMAND_FAILED);
        m_context.set_last_error(error.create_last_error());

        if (!m_sCommand->is_silent())
        {
            response.reset(error.create_response(*m_sCommand), Command::Response::Status::NOT_CACHEABLE);
        }
    }

    if (state == State::READY)
    {
        if (m_pCache_filter_session && response.invalidated())
        {
            const auto& config = m_pCache_filter_session->config();

            if (config.invalidate == CACHE_INVALIDATE_CURRENT)
            {
                std::string table = m_sCommand->table(Command::Quoted::NO);
                mxb_assert(!table.empty());

                if (config.debug & CACHE_DEBUG_DECISIONS)
                {
                    MXB_NOTICE("Invalidating NoSQL responses related to table '%s'.", table.c_str());
                }

                std::vector<std::string> invalidation_words { table };

                MXB_AT_DEBUG(cache_result_t rv =) m_pCache_filter_session->invalidate(invalidation_words,
                                                                                      nullptr);
                mxb_assert(CACHE_RESULT_IS_OK(rv));
            }
        }

        response.set_command(std::move(m_sCommand));
        set_ready();
    }

    return response;
}

Command::Response Database::get_cached_response(const std::string& name,
                                                GWBUF* pNoSQL_request,
                                                const packet::Msg& req,
                                                CacheKey* pKey)
{
    mxb_assert(m_pCache_filter_session);

    Command::Response response;

    auto& user = m_pCache_filter_session->user();
    auto& host = m_pCache_filter_session->user();
    auto* zDefault_db = m_pCache_filter_session->default_db();

    *pKey = nosql::cache::get_key(user, host, zDefault_db, req.document());

    GWBUF value;
    auto rv = m_pCache_filter_session->get_value(*pKey, 0, &value, nullptr);
    mxb_assert(!CACHE_RESULT_IS_PENDING(rv));

    auto debug = m_pCache_filter_session->config().debug;

    if (CACHE_RESULT_IS_OK(rv))
    {
        if (debug & CACHE_DEBUG_DECISIONS)
        {
            MXB_NOTICE("Response to NoSQL command '%s' was FOUND in cache.", name.c_str());
        }

        Command::ResponseChecksum response_checksum =
            req.checksum_present() ? Command::ResponseChecksum::UPDATE : Command::ResponseChecksum::RESET;

        Command::patch_response(value, m_context.next_request_id(), req.request_id(), response_checksum);

        response.reset(nosql::gwbuf_to_gwbufptr(std::move(value)), Command::Response::Status::NOT_CACHEABLE);
    }
    else
    {
        if (debug & CACHE_DEBUG_DECISIONS)
        {
            MXB_NOTICE("Response to NoSQL command '%s' was NOT found in cache.", name.c_str());
        }
    }

    return response;
}

State Database::execute_command(std::unique_ptr<Command> sCommand, Command::Response* pResponse)
{
    State state = State::READY;
    Command::Response response;

    bool ready = true;

    auto& session = m_context.session();

    if (sCommand->session_must_be_ready() && !session.is_alive())
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
                response.reset(x.create_response(*m_sCommand.get()), Command::Response::Status::NOT_CACHEABLE);
            }
        }
        catch (const bsoncxx::exception& x)
        {
            MXB_ERROR("bsoncxx exeception occurred when parsing NoSQL command: %s", x.what());

            HardError error(x.what(), error::FAILED_TO_PARSE);
            m_context.set_last_error(error.create_last_error());

            if (!m_sCommand->is_silent())
            {
                response.reset(error.create_response(*m_sCommand), Command::Response::Status::NOT_CACHEABLE);
            }
        }
        catch (const std::exception& x)
        {
            MXB_ERROR("std exception occurred when parsing NoSQL command: %s", x.what());

            HardError error(x.what(), error::FAILED_TO_PARSE);
            m_context.set_last_error(error.create_last_error());

            if (!m_sCommand->is_silent())
            {
                response.reset(error.create_response(*m_sCommand), Command::Response::Status::NOT_CACHEABLE);
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
