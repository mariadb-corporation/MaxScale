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
#include <maxscale/target.hh>
#include "mxsmongo.hh"

namespace mxsmongo
{

class Database
{
public:
    enum State
    {
        READY,  // Ready for a command.
        PENDING // A command is being executed.
    };

    /**
     * @param name  The Mongo database in question.
     */
    Database(const std::string& name, Mongo::Context* pContext);
    ~Database();

    Database(const Database&) = delete;
    Database& operator = (const Database&) = delete;

    /**
     * Run a Mongo command.
     *
     * @req         The command to be run. *Must* be intended for the database this
     *              instance represents.
     * @downstream  The downstream component the command should be (suitably translated)
     *              be sent to, if the response cannot be generated immediately.
     *
     * @return A GWBUF containing a Mongo response, or nullptr. In the former case
     *         it will be returned to the client, in the latter case @c clientReply
     *         of the client protocol will eventually be called.
     */
    GWBUF* run_command(const mxsmongo::Query& req, mxs::Component& downstream);
    GWBUF* run_command(const mxsmongo::Msg& req,  mxs::Component& downstream);

    /**
     * Convert a MariaDB response to a Mongo response. Must only be called
     * if an earlier call to @c run_command returned @ nullptr and only with
     * the buffer delivered to @c clientReply of the client protocol.
     *
     * @param pMariaDB_response  A response as received from downstream.
     *
     * @return @c pMariaDB_response translated into the equivalent Mongo response.
     */
    GWBUF* translate(GWBUF* pMariaDB_response);

private:
    bool is_ready() const
    {
        return m_state == READY;
    }

    bool is_pending() const
    {
        return m_state == PENDING;
    }

    State           m_state { READY };
    std::string     m_name;
    Mongo::Context& m_context;

};
}
