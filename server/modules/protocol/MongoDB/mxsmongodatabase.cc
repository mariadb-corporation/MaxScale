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

#include "mxsmongodatabase.hh"
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "../../filter/masking/mysql.hh"

using namespace std;

class mxsmongo::Database::Command
{
public:
    Command(mxsmongo::Database* pDatabase,
            GWBUF* pRequest,
            const mxsmongo::Packet& req,
            const bsoncxx::document::view& doc)
        : m_database(*pDatabase)
        , m_pRequest(gwbuf_clone(pRequest))
        , m_req(req)
        , m_doc(doc)
    {
    }

    virtual ~Command()
    {
        free_request();
    }

    virtual GWBUF* execute() = 0;

    virtual GWBUF* translate(GWBUF& mariadb_response)
    {
        mxb_assert(!true);
        return nullptr;
    }

protected:
    void free_request()
    {
        if (m_pRequest)
        {
            gwbuf_free(m_pRequest);
            m_pRequest = nullptr;
        }
    }

    pair<GWBUF*, uint8_t*> create_response(size_t size_of_documents, size_t nDocuments)
    {
        // TODO: In the following is assumed that whatever is returned will
        // TODO: fit into a Mongo packet.

        int32_t response_flags = MONGOC_QUERY_AWAIT_DATA; // Dunno if this should be on.
        int64_t cursor_id = 0;
        int32_t starting_from = 0;
        int32_t number_returned = nDocuments;

        size_t response_size = MXSMONGO_HEADER_LEN
            + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
            + size_of_documents;

        GWBUF* pResponse = gwbuf_alloc(response_size);

        auto* pRes_hdr = reinterpret_cast<mongoc_rpc_header_t*>(GWBUF_DATA(pResponse));
        pRes_hdr->msg_len = response_size;
        pRes_hdr->request_id = m_database.context().next_request_id();
        pRes_hdr->response_to = m_req.request_id();
        pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

        uint8_t* pData = GWBUF_DATA(pResponse) + MXSMONGO_HEADER_LEN;

        pData += mxsmongo::set_byte4(pData, response_flags);
        pData += mxsmongo::set_byte8(pData, cursor_id);
        pData += mxsmongo::set_byte4(pData, starting_from);
        pData += mxsmongo::set_byte4(pData, number_returned);

        return make_pair(pResponse, pData);
    }

    GWBUF* create_response(size_t size_of_documents, const vector<bsoncxx::document::value>& documents)
    {
        GWBUF* pResponse;
        uint8_t* pData;

        tie(pResponse, pData) = create_response(size_of_documents, documents.size());

        for (const auto& doc : documents)
        {
            auto view = doc.view();
            size_t size = view.length();

            memcpy(pData, view.data(), view.length());
            pData += view.length();
        }

        return pResponse;
    }

    GWBUF* create_response(const bsoncxx::document::value& doc)
    {
        auto doc_view = doc.view();
        size_t doc_len = doc_view.length();

        GWBUF* pResponse;
        uint8_t* pData;

        tie(pResponse, pData) = create_response(doc_len, 1);

        memcpy(pData, doc_view.data(), doc_view.length());

        return pResponse;
    }

    GWBUF* create_empty_response()
    {
        auto builder = bsoncxx::builder::stream::document{};
        bsoncxx::document::value doc_value = builder << bsoncxx::builder::stream::finalize;

        return create_response(doc_value);
    }

    GWBUF* translate_resultset(GWBUF& mariadb_response)
    {
        bsoncxx::builder::basic::document builder;

        uint8_t* pBuffer = GWBUF_DATA(&mariadb_response);

        // A result set, so first we get the number of fields...
        ComQueryResponse cqr(&pBuffer);

        auto nFields = cqr.nFields();

        vector<string> names;
        vector<enum_field_types> types;

        for (size_t i = 0; i < nFields; ++i)
        {
            // ... and then as many column definitions.
            ComQueryResponse::ColumnDef column_def(&pBuffer);

            names.push_back(column_def.name().to_string());
            types.push_back(column_def.type());
        }

        // The there should be an EOF packet, which should be bypassed.
        ComResponse eof(&pBuffer);
        mxb_assert(eof.type() == ComResponse::EOF_PACKET);

        vector<bsoncxx::document::value> documents;
        uint32_t size_of_documents = 0;

        // Then there will be an arbitrary number of rows. After all rows
        // (of which there obviously may be 0), there will be an EOF packet.
        while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
        {
            CQRTextResultsetRow row(&pBuffer, types);

            auto it = names.begin();
            auto jt = row.begin();

            while (it != names.end())
            {
                const string& name = *it;
                const auto& value = *jt;

                if (value.is_string())
                {
                    builder.append(bsoncxx::builder::basic::kvp(name, value.as_string().to_string()));
                }
                else
                {
                    // TODO: Handle other types as well.
                    builder.append(bsoncxx::builder::basic::kvp(name, ""));
                }

                auto doc = builder.extract();
                size_of_documents += doc.view().length();

                documents.push_back(doc);

                ++it;
                ++jt;
            }
        }

        return create_response(size_of_documents, documents);
    }

    mxsmongo::Database&     m_database;
    GWBUF*                  m_pRequest;
    mxsmongo::Packet        m_req;
    bsoncxx::document::view m_doc;
};

namespace
{


namespace command
{

// TODO: This will be generalized so that there will be e.g. a base-class ResultSet for
// TODO: commands that expects, well, a resultset. But for now there is no hierarchy.

class Find : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;
        sql << "SELECT ";

        auto projection = m_doc[mxsmongo::keys::PROJECTION];

        if (projection)
        {
            sql << get_columns(projection);
        }
        else
        {
            sql << "*";
        }

        sql << " FROM ";

        auto element = m_doc[mxsmongo::keys::FIND];

        mxb_assert(element.type() == bsoncxx::type::k_utf8);

        auto utf8 = element.get_utf8();

        string table(utf8.value.data(), utf8.value.size());

        sql << m_database.name() << "." << table;

        MXS_NOTICE("SQL: %s", sql.str().c_str());

        GWBUF* pRequest = modutil_create_query(sql.str().c_str());

        m_database.context().downstream().routeQuery(pRequest);

        return nullptr;
    }

    GWBUF* translate(GWBUF& mariadb_response) override
    {
        // TODO: Update will be needed when DEPRECATE_EOF it turned on.
        GWBUF* pResponse = nullptr;

        ComResponse response(GWBUF_DATA(&mariadb_response));

        switch (response.type())
        {
        case ComResponse::ERR_PACKET:
            MXS_WARNING("Mongo request to backend failed: (%d), %s",
                        mxs_mysql_get_mysql_errno(&mariadb_response),
                        mxs::extract_error(&mariadb_response).c_str());
            pResponse = create_empty_response();
            break;

        case ComResponse::OK_PACKET:
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            // This should not happen as the respon
            mxb_assert(!true);
            break;

        default:
            // Must be a result set.
            pResponse = translate_resultset(mariadb_response);
        }

        return pResponse;
    }

private:
    string get_columns(const bsoncxx::document::element& projection)
    {
        vector<string> columns;

        if (projection.type() == bsoncxx::type::k_document)
        {
            bsoncxx::document::view doc = projection.get_document();

            for (auto it = doc.begin(); it != doc.end(); ++it)
            {
                const auto& element = *it;
                const auto& key = element.key();

                string column { key.data(), key.size() };

                if (column != "_id")
                {
                    // TODO: Could something meaningful be returned for _id?
                    columns.push_back(column);
                }
            }
        }
        else
        {
            MXS_ERROR("'%s' is not an object, returning all columns.", mxsmongo::keys::PROJECTION);
        }

        if (columns.empty())
        {
            columns.push_back("*");
        }

        return mxb::join(columns);
    }
};

class IsMaster : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        // TODO: Do not simply return a hardwired response.

        auto builder = bsoncxx::builder::stream::document{};
        bsoncxx::document::value doc_value = builder
            << "ismaster" << true
            << "topologyVersion" << mxsmongo::topology_version()
            << "maxBsonObjectSize" << (int32_t)16777216
            << "maxMessageSizeBytes" << (int32_t)48000000
            << "maxWriteBatchSize" << (int32_t)100000
            << "localTime" << bsoncxx::types::b_date(std::chrono::system_clock::now())
            << "logicalSessionTimeoutMinutes" << (int32_t)30
            << "connectionId" << (int32_t)4
            << "minWireVersion" << (int32_t)0
            << "maxWireVersion" << (int32_t)9
            << "readOnly" << false
            << "ok" << (double)1
            << bsoncxx::builder::stream::finalize;

        return create_response(doc_value);
    }
};

class Unknown : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        MXS_ERROR("Command not recognized: %s", m_req.to_string().c_str());

        // Inconvenient during development if every single unknown command leads
        // to an abort. Now optionally an empty document may be returned instead.
        mxb_assert(mxsmongo::continue_on_unknown());

        return create_empty_response();
    }
};


template<class ConcreteCommand>
unique_ptr<mxsmongo::Database::Command> create(mxsmongo::Database* pDatabase,
                                               GWBUF* pRequest,
                                               const mxsmongo::Packet& req,
                                               const bsoncxx::document::view& doc)
{
    return unique_ptr<ConcreteCommand>(new ConcreteCommand(pDatabase, pRequest, req, doc));
}

}

struct ThisUnit
{
    const map<mxsmongo::Command,
              unique_ptr<mxsmongo::Database::Command> (*)(mxsmongo::Database* pDatabase,
                                                          GWBUF* pRequest,
                                                          const mxsmongo::Packet& req,
                                                          const bsoncxx::document::view& doc)>
    creators_by_command =
    {
        { mxsmongo::Command::FIND,     &command::create<command::Find> },
        { mxsmongo::Command::ISMASTER, &command::create<command::IsMaster> },
        { mxsmongo::Command::UNKNOWN,  &command::create<command::Unknown> }
    };
} this_unit;

}

mxsmongo::Database::Database(const std::string& name, Mongo::Context* pContext)
    : m_name(name)
    , m_context(*pContext)
{
}

mxsmongo::Database::~Database()
{
    mxb_assert(m_state == READY);
}

//static
unique_ptr<mxsmongo::Database> mxsmongo::Database::create(const std::string& name, Mongo::Context* pContext)
{
    return unique_ptr<Database>(new Database(name, pContext));
}

GWBUF* mxsmongo::Database::handle_query(GWBUF* pRequest, const mxsmongo::Query& req)
{
    mxb_assert(is_ready());

    return execute(mxsmongo::get_command(req.query()), pRequest, req, req.query());
}

GWBUF* mxsmongo::Database::handle_command(GWBUF* pRequest,
                                          const mxsmongo::Msg& req,
                                          const bsoncxx::document::view& doc)
{
    mxb_assert(is_ready());

    return execute(mxsmongo::get_command(doc), pRequest, req, doc);
}

GWBUF* mxsmongo::Database::translate(GWBUF& mariadb_response)
{
    mxb_assert(is_pending());
    mxb_assert(m_sCommand.get());

    GWBUF* pResponse = m_sCommand->translate(mariadb_response);

    m_sCommand.reset();

    set_ready();

    return pResponse;
}

GWBUF* mxsmongo::Database::execute(mxsmongo::Command cid,
                                   GWBUF* pRequest,
                                   const mxsmongo::Packet& req,
                                   const bsoncxx::document::view& doc)
{
    GWBUF* pResponse = nullptr;

    auto it = this_unit.creators_by_command.find(cid);
    mxb_assert(it != this_unit.creators_by_command.end());

    auto sCommand = it->second(this, pRequest, req, doc);

    pResponse = sCommand->execute();

    if (!pResponse)
    {
        m_sCommand = std::move(sCommand);
        set_pending();
    }

    return pResponse;
}
