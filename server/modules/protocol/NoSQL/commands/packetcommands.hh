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

//
// https://docs.mongodb.com/v4.4/reference/mongodb-wire-protocol/#op_insert
//
#include "defs.hh"
#include <maxbase/worker.hh>
#include "../nosqlcursor.hh"

namespace nosql
{

namespace command
{

class InsertCommand : public Command
{
public:
    using Worker = mxb::Worker;

    enum Action
    {
        INSERTING_DATA,
        CREATING_TABLE,
        CREATING_DATABASE
    };

    InsertCommand(Database* pDatabase,
                  GWBUF* pRequest,
                  const nosql::Insert& req)
        : Command(pDatabase, pRequest, req.request_id(), ResponseKind::REPLY)
        , m_action(INSERTING_DATA)
        , m_table(req.collection())
        , m_documents(req.documents())
    {
        mxb_assert(m_documents.size() == 1);
    }

    string description() const override
    {
        return "OP_INSERT";
    }

    GWBUF* execute() override final
    {
        auto doc = m_documents[0];

        ostringstream ss;
        ss << "INSERT INTO " << m_table << " (doc) VALUES " << convert_document_data(doc) << ";";

        m_statement = ss.str();

        check_maximum_sql_length(m_statement);

        send_downstream(m_statement);

        return nullptr;
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        State state = READY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            if (m_action == CREATING_TABLE || m_action == CREATING_DATABASE)
            {
                Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                        if (action == Worker::Call::EXECUTE)
                        {
                            m_action = INSERTING_DATA;
                            send_downstream(m_statement);
                        }

                        return false;
                    });
                state = BUSY;
            }
            else
            {
                state = READY;
            }
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);
                auto s = err.message();
                MXS_INFO("%s", s.c_str());

                switch (err.code())
                {
                case ER_NO_SUCH_TABLE:
                    {
                        Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                                if (action == Worker::Call::EXECUTE)
                                {
                                    auto id_length = m_database.config().id_length;
                                    auto sql = nosql::table_create_statement(m_table, id_length);

                                    m_action = CREATING_TABLE;
                                    send_downstream(sql);
                                }

                                return false;
                            });
                        state = BUSY;
                    }
                    break;

                case ER_BAD_DB_ERROR:
                    {
                        if (err.message().find("Unknown database") == 0)
                        {
                            Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                                    if (action == Worker::Call::EXECUTE)
                                    {
                                        ostringstream ss;
                                        ss << "CREATE DATABASE `" << m_database.name() << "`";

                                        m_action = CREATING_DATABASE;
                                        send_downstream(ss.str());
                                    }

                                    return false;
                                });
                            state = BUSY;
                        }
                        else
                        {
                            MXS_ERROR("Inserting '%s' failed with: (%d) %s",
                                      m_statement.c_str(), err.code(), err.message().c_str());
                            state = READY;
                        }
                    }
                    break;

                case ER_DB_CREATE_EXISTS:
                case ER_TABLE_EXISTS_ERROR:
                    // Ok, someone else got there first.
                    Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                            if (action == Worker::Call::EXECUTE)
                            {
                                m_action = INSERTING_DATA;
                                send_downstream(m_statement);
                            }

                            return false;
                        });
                    state = BUSY;
                    break;

                default:
                    MXS_ERROR("Inserting '%s' failed with: (%d) %s",
                              m_statement.c_str(), err.code(), err.message().c_str());
                    state = READY;
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppNoSQL_response = pResponse;
        return state;
    };

    void diagnose(DocumentBuilder& doc) override final
    {
        mxb_assert(!true);
    }

private:
    string convert_document_data(const bsoncxx::document::view& doc)
    {
        ostringstream sql;

        string json;

        auto element = doc["_id"];

        if (element)
        {
            json = bsoncxx::to_json(doc);
        }
        else
        {
            // Ok, as the document does not have an id, one must be generated. However,
            // as an existing document is immutable, a new one must be created.

            bsoncxx::oid oid;

            DocumentBuilder builder;
            builder.append(kvp(key::_ID, oid));

            for (const auto& e : doc)
            {
                append(builder, e.key(), e);
            }

            // We need to keep the created document around, so that 'element'
            // down below stays alive.
            m_stashed_documents.emplace_back(builder.extract());

            const auto& doc_with_id = m_stashed_documents.back();

            json = bsoncxx::to_json(doc_with_id);
        }

        json = escape_essential_chars(std::move(json));

        sql << "('" << json << "')";

        return sql.str();
    }

private:
    Action                           m_action;
    string                           m_table;
    string                           m_statement;
    vector<bsoncxx::document::view>  m_documents;
    vector<bsoncxx::document::value> m_stashed_documents;
};

}
}
