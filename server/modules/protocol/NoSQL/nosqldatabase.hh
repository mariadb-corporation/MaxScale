/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <memory>
#include <maxscale/target.hh>
#include "nosqlcommon.hh"
#include "nosqlcommand.hh"

class Config;

namespace nosql
{

class Database
{
public:
    ~Database();

    Database(const Database&) = delete;
    Database& operator = (const Database&) = delete;

    /**
     * @return The name of the database.
     */
    const std::string& name() const
    {
        return m_name;
    }

    /**
     * @return The context.
     */
    Context& context()
    {
        return m_context;
    }

    const Context& context() const
    {
        return m_context;
    }

    /**
     * @return The config.
     */
    const Config& config() const
    {
        return m_config;
    }

    Config& config()
    {
        return m_config;
    }

    /**
     * Create a new instance.
     *
     * @param name      The database in question.
     * @param pContext  The context to be used, a reference will be stored.
     * @param pConfig   The configuration.
     *
     * @return Unique pointer to the instance.
     */
    static std::unique_ptr<Database> create(const std::string& name,
                                            Context* pContext,
                                            Config* pConfig,
                                            CacheFilterSession* pCache_filter_session);

    State handle_delete(GWBUF* pRequest, packet::Delete&& req, Command::Response* pResponse);
    State handle_insert(GWBUF* pRequest, packet::Insert&& req, Command::Response* pResponse);
    State handle_query(GWBUF* pRequest, packet::Query&& req, Command::Response* pResponse);
    State handle_update(GWBUF* pRequest, packet::Update&& req, Command::Response* pResponse);
    State handle_get_more(GWBUF* pRequest, packet::GetMore&& req, Command::Response* pResponse);
    State handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, Command::Response* pResponse);
    State handle_msg(GWBUF* pRequest, packet::Msg&& req, Command::Response* pResponse);

    /**
     * Convert a MariaDB response to a NoSQL response. Must only be called
     * if an earlier call to @c run_command returned @ nullptr and only with
     * the buffer delivered to @c clientReply of the client protocol.
     *
     * @param mariadb_response  A response as received from downstream.
     *
     * @return @c mariadb_response translated into the equivalent NoSQL response.
     */
    Command::Response translate(GWBUF&& mariadb_response);

    /**
     * @return True, if there is no pending activity, false otherwise.
     */
    bool is_ready() const
    {
        return m_state == State::READY;
    }

private:
    Database(const std::string& name,
             Context* pContext,
             Config* pConfig,
             CacheFilterSession* pCache_filter_session);

    bool is_busy() const
    {
        return m_state == State::BUSY;
    }

    void set_busy()
    {
        m_state = State::BUSY;
    }

    void set_ready()
    {
        m_state = State::READY;
    }

    Command::Response get_cached_response(const std::string& name,
                                          GWBUF* pRequest,
                                          const packet::Msg& req,
                                          CacheKey* pKey);

    State execute_command(std::unique_ptr<Command> sCommand, Command::Response* pResponse);

    using SCommand = std::unique_ptr<Command>;

    State               m_state { State::READY };
    const std::string   m_name;
    Context&            m_context;
    Config&             m_config;
    SCommand            m_sCommand;
    CacheFilterSession* m_pCache_filter_session;
};
}
