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

#include "nosqlprotocol.hh"
#include <memory>
#include <maxscale/target.hh>
#include "nosql.hh"
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
    NoSQL::Context& context()
    {
        return m_context;
    }

    const NoSQL::Context& context() const
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
                                            NoSQL::Context* pContext,
                                            Config* pConfig);

    State handle_delete(GWBUF* pRequest, packet::Delete&& req, GWBUF** ppResponse);
    State handle_insert(GWBUF* pRequest, packet::Insert&& req, GWBUF** ppResponse);
    State handle_query(GWBUF* pRequest, packet::Query&& req, GWBUF** ppResponse);
    State handle_update(GWBUF* pRequest, packet::Update&& req, GWBUF** ppResponse);
    State handle_get_more(GWBUF* pRequest, packet::GetMore&& req, GWBUF** ppResponse);
    State handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, GWBUF** ppResponse);
    State handle_msg(GWBUF* pRequest, packet::Msg&& req, GWBUF** ppResponse);

    /**
     * Convert a MariaDB response to a NoSQL response. Must only be called
     * if an earlier call to @c run_command returned @ nullptr and only with
     * the buffer delivered to @c clientReply of the client protocol.
     *
     * @param mariadb_response  A response as received from downstream.
     *
     * @return @c mariadb_response translated into the equivalent NoSQL response.
     */
    GWBUF* translate(mxs::Buffer&& mariadb_response);

    /**
     * @return True, if there is no pending activity, false otherwise.
     */
    bool is_ready() const
    {
        return m_state == State::READY;
    }

private:
    Database(const std::string& name,
             NoSQL::Context* pContext,
             Config* pConfig);

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

    State execute_command(std::unique_ptr<Command> sCommand, GWBUF** ppResponse);

    using SCommand = std::unique_ptr<Command>;

    State             m_state { State::READY };
    const std::string m_name;
    NoSQL::Context&   m_context;
    Config&           m_config;
    SCommand          m_sCommand;
};
}
