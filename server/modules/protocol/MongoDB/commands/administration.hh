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

//
// https://docs.mongodb.com/manual/reference/command/nav-administration/
//

#include "defs.hh"

namespace mxsmongo
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/cloneCollectionAsCapped/

// https://docs.mongodb.com/manual/reference/command/collMod/

// https://docs.mongodb.com/manual/reference/command/compact/

// https://docs.mongodb.com/manual/reference/command/connPoolSync/

// https://docs.mongodb.com/manual/reference/command/convertToCapped/

// https://docs.mongodb.com/manual/reference/command/create/
class Create : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;

        sql << "CREATE TABLE " << table() << " (id TEXT NOT NULL UNIQUE, doc JSON)";

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(GWBUF_DATA(&mariadb_response));

        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_TABLE_EXISTS_ERROR)
                {
                    stringstream ss;
                    ss << "Collection already exists. NS: " << table(Quoted::NO);
                    throw SoftError(ss.str(), error::NAMESPACE_EXISTS);
                }
                else
                {
                    throw MariaDBError(ComERR(response));
                }
            }
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            break;

        default:
            mxb_assert(!true);
        }

        DocumentBuilder doc;

        doc.append(kvp("ok", ok));

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/createIndexes/

// https://docs.mongodb.com/manual/reference/command/currentOp/

// https://docs.mongodb.com/manual/reference/command/drop/
class Drop : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;

        sql << "DROP TABLE " << table();

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(GWBUF_DATA(&mariadb_response));

        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_BAD_TABLE_ERROR)
                {
                    throw SoftError("ns not found", error::NAMESPACE_NOT_FOUND);
                }
                else
                {
                    throw MariaDBError(ComERR(response));
                }
            }
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            break;

        default:
            mxb_assert(!true);
        }

        DocumentBuilder doc;

        doc.append(kvp("ok", ok));
        doc.append(kvp("ns", table(Quoted::NO)));
        doc.append(kvp("nIndexesWas", 1)); // TODO: Report real value.

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/dropDatabase/
class DropDatabase : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;

        sql << "DROP DATABASE `" << m_database.name() << "`";

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(GWBUF_DATA(&mariadb_response));

        DocumentBuilder doc;
        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            doc.append(kvp("dropped", m_database.name()));
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_DB_DROP_EXISTS)
                {
                    // Report with "ok" == 1, but without "dropped".
                }
                else
                {
                    throw MariaDBError(err);
                }
            }
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            break;

        default:
            mxb_assert(!true);
        }

        doc.append(kvp("ok", ok));

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/dropConnections/

// https://docs.mongodb.com/manual/reference/command/dropIndexes/

// https://docs.mongodb.com/manual/reference/command/filemd5/

// https://docs.mongodb.com/manual/reference/command/fsync/

// https://docs.mongodb.com/manual/reference/command/fsyncUnlock/

// https://docs.mongodb.com/manual/reference/command/getDefaultRWConcern/

// https://docs.mongodb.com/manual/reference/command/getParameter/

// https://docs.mongodb.com/manual/reference/command/killCursors/

// https://docs.mongodb.com/manual/reference/command/killOp/

// https://docs.mongodb.com/manual/reference/command/listCollections/
class ListCollections : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        optional(key::NAMEONLY, &m_name_only, Conversion::RELAXED);

        bsoncxx::document::view filter;
        if (optional(key::FILTER, &filter))
        {
            MXS_WARNING("listCollections.filter is ignored.");
        }

        stringstream sql;
        sql << "SHOW TABLES FROM `" << m_database.name() << "`";

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(GWBUF_DATA(&mariadb_response));

        DocumentBuilder doc;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            mxb_assert(!true);
            break;

        case ComResponse::ERR_PACKET:
            throw MariaDBError(ComERR(response));
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            break;

        default:
            {
                uint8_t* pBuffer = gwbuf_link_data(&mariadb_response);

                ComQueryResponse cqr(&pBuffer);
                auto nFields = cqr.nFields();
                mxb_assert(nFields == 1);

                vector<enum_field_types> types;

                for (size_t i = 0; i < nFields; ++i)
                {
                    ComQueryResponse::ColumnDef column_def(&pBuffer);

                    types.push_back(column_def.type());
                }

                ComResponse eof(&pBuffer);
                mxb_assert(eof.type() == ComResponse::EOF_PACKET);

                ArrayBuilder firstBatch;

                while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
                {
                    CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer
                    auto it = row.begin();

                    auto table = (*it++).as_string().to_string();
                    mxb_assert(it == row.end());

                    DocumentBuilder collection;
                    collection.append(kvp("name", table));
                    collection.append(kvp("type", "collection"));
                    if (!m_name_only)
                    {
                        DocumentBuilder options;
                        DocumentBuilder info;
                        info.append(kvp("readOnly", false));
                        //info.append(kvp("uuid", ...); // TODO: Could something meaningful be added here?
                        // DocumentBuilder idIndex;
                        // idIndex.append(kvp("v", ...));
                        // idIndex.append(kvp("key", ...));
                        // idIndex.append(kvp("name", ...));

                        collection.append(kvp("options", options.extract()));
                        collection.append(kvp("info", info.extract()));
                        //collection.append(kvp("idIndex", idIndex.extract()));
                    }

                    firstBatch.append(collection.extract());
                }

                string ns = m_database.name() + ".$cmd.listCollections";

                DocumentBuilder cursor;
                cursor.append(kvp("id", int64_t(0)));
                cursor.append(kvp("ns", ns));
                cursor.append(kvp("firstBatch", firstBatch.extract()));

                doc.append(kvp("cursor", cursor.extract()));
                doc.append(kvp("ok", 1));
            }
        }

        *ppResponse = create_response(doc.extract());
        return READY;
    }

private:
    bool m_name_only { false };
};


// https://docs.mongodb.com/manual/reference/command/listDatabases/
class ListDatabases : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        if (m_database.name() != "admin")
        {
            throw SoftError("listDatabases may only be run against the admin database.",
                            error::UNAUTHORIZED);
        }

        stringstream sql;
        sql << "SELECT table_schema, table_name, (data_length + index_length) `bytes` "
            << "FROM information_schema.tables "
            << "WHERE table_schema NOT IN ('information_schema', 'performance_schema', 'mysql')";

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(GWBUF_DATA(&mariadb_response));

        DocumentBuilder doc;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            mxb_assert(!true);
            break;

        case ComResponse::ERR_PACKET:
            throw MariaDBError(ComERR(response));
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            break;

        default:
            {
                uint8_t* pBuffer = gwbuf_link_data(&mariadb_response);

                ComQueryResponse cqr(&pBuffer);
                auto nFields = cqr.nFields();

                vector<enum_field_types> types;

                for (size_t i = 0; i < nFields; ++i)
                {
                    ComQueryResponse::ColumnDef column_def(&pBuffer);

                    types.push_back(column_def.type());
                }

                ComResponse eof(&pBuffer);
                mxb_assert(eof.type() == ComResponse::EOF_PACKET);

                map<string, int32_t> size_by_db;
                int32_t total_size = 0;

                while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
                {
                    CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer
                    auto it = row.begin();

                    auto table_schema = (*it++).as_string().to_string();
                    auto table_name = (*it++).as_string().to_string();
                    auto bytes = std::stol((*it++).as_string().to_string());
                    mxb_assert(it == row.end());

                    size_by_db[table_schema] += bytes;
                    total_size += bytes;
                }

                ArrayBuilder databases;

                for (const auto& kv : size_by_db)
                {
                    const auto& name = kv.first;
                    const auto& bytes = kv.second;

                    DocumentBuilder database;
                    database.append(kvp("name", name));
                    database.append(kvp("sizeOnDisk", bytes));
                    database.append(kvp("empty", bytes == 0));

                    databases.append(database.extract());
                }

                doc.append(kvp("databases", databases.extract()));
                doc.append(kvp("totalSize", total_size));
                doc.append(kvp("ok", 1));
            }
        }

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/listIndexes/

// https://docs.mongodb.com/manual/reference/command/logRotate/

// https://docs.mongodb.com/manual/reference/command/reIndex/

// https://docs.mongodb.com/manual/reference/command/renameCollection/

// https://docs.mongodb.com/manual/reference/command/setFeatureCompatibilityVersion/

// https://docs.mongodb.com/manual/reference/command/setIndexCommitQuorum/

// https://docs.mongodb.com/manual/reference/command/setParameter/

// https://docs.mongodb.com/manual/reference/command/setDefaultRWConcern/

// https://docs.mongodb.com/manual/reference/command/shutdown/


}

}
