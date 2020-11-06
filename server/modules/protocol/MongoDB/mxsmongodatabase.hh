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

#pragma once

#include "mongodbclient.hh"
#include <memory>
#include <maxscale/target.hh>
#include "mxsmongo.hh"

namespace mxsmongo
{

class Database
{
public:
    class Command;

    enum State
    {
        READY,  // Ready for a command.
        PENDING // A command is being executed.
    };

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
    Mongo::Context& context()
    {
        return m_context;
    }

    /**
     * Create a new instance.
     *
     * @param name      The Mongo database in question.
     * @param pContext  The context to be used, a reference will be stored.
     *
     * @return Unique pointer to the instance.
     */
    static std::unique_ptr<Database> create(const std::string& name, Mongo::Context* pContext);

    /**
     * Handle a Mongo query.
     *
     * @pRequest    The GWBUF holding data of @c req.
     * @req         The query request; *must* be intended for the database this
     *              instance represents.
     *
     * @return A GWBUF containing a Mongo response, or nullptr. In the former case
     *         it will be returned to the client, in the latter case @c clientReply
     *         of the client protocol will eventually be called.
     */
    GWBUF* handle_query(GWBUF* pRequest, const mxsmongo::Query& req);

    /**
     * Handle a Mongo command.
     *
     * @pRequest    The GWBUF holding data of @c req.
     * @req         The message request.
     * @doc         The document containing the command; *must* be intended for the
     *              database this instance represents.
     *
     * @return A GWBUF containing a Mongo response, or nullptr. In the former case
     *         it will be returned to the client, in the latter case @c clientReply
     *         of the client protocol will eventually be called.
     */
    GWBUF* handle_command(GWBUF* pRequest,
                          const mxsmongo::Msg& req,
                          const bsoncxx::document::view& doc);

    /**
     * Convert a MariaDB response to a Mongo response. Must only be called
     * if an earlier call to @c run_command returned @ nullptr and only with
     * the buffer delivered to @c clientReply of the client protocol.
     *
     * @param mariadb_response  A response as received from downstream.
     *
     * @return @c mariadb_response translated into the equivalent Mongo response.
     */
    GWBUF* translate(GWBUF& mariadb_response);

private:
    Database(const std::string& name, Mongo::Context* pContext);

    bool is_ready() const
    {
        return m_state == READY;
    }

    bool is_pending() const
    {
        return m_state == PENDING;
    }

    void set_pending()
    {
        m_state = PENDING;
    }

    void set_ready()
    {
        m_state = READY;
    }

    GWBUF* execute(mxsmongo::Command command,
                   GWBUF* pRequest,
                   const mxsmongo::Packet& req,
                   const bsoncxx::document::view& doc);

    using SCommand = std::unique_ptr<Command>;

    State             m_state { READY };
    const std::string m_name;
    Mongo::Context&   m_context;
    SCommand          m_sCommand;
};
}
