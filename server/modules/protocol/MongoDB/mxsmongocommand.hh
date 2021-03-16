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
#include <string>
#include <utility>
#include <vector>
#include <bsoncxx/builder/basic/document.hpp>
#include <maxscale/buffer.hh>
#include "../../filter/masking/mysql.hh"
#include "mxsmongo.hh"

namespace mxsmongo
{

class Database;

class CommandX
{
public:
    CommandX(Database* pDatabase,
             GWBUF* pRequest,
             const Packet& req,
             const bsoncxx::document::view& doc);

    enum State
    {
        BUSY,
        READY
    };

    virtual ~CommandX();

    virtual GWBUF* execute() = 0;

    virtual State translate(GWBUF& mariadb_response, GWBUF** ppMongo_response);

    GWBUF* create_empty_response();

    GWBUF* create_error_response(const std::string& message, error::Code code);

protected:
    std::string get_table(const char* zCommand) const;

    void free_request();

    void send_downstream(const std::string& sql);

    GWBUF* create_response(const bsoncxx::document::value& doc);

    GWBUF* translate_resultset(std::vector<std::string>& extractions, GWBUF* pMariadb_response);

    void add_error(bsoncxx::builder::basic::document& builder, const ComERR& err);

    Database&               m_database;
    GWBUF*                  m_pRequest;
    Packet                  m_req;
    bsoncxx::document::view m_doc;

private:
    std::pair<GWBUF*, uint8_t*> create_reply_response_buffer(size_t size_of_documents, size_t nDocuments);

    GWBUF* create_reply_response(size_t size_of_documents, const std::
                                 vector<bsoncxx::document::value>& documents);
    GWBUF* create_reply_response(const bsoncxx::document::value& doc);

    GWBUF* create_msg_response(const bsoncxx::document::value& doc);

    std::string create_leaf_entry(const std::string& extraction, const std::string& value);

    std::string create_nested_entry(const std::string& extraction, const std::string& value);

    std::string create_entry(const std::string& extraction, const std::string& value);
};

}
