/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/manual/reference/command/nav-administration/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/cloneCollectionAsCapped/

// https://docs.mongodb.com/manual/reference/command/collMod/

// https://docs.mongodb.com/manual/reference/command/compact/

// https://docs.mongodb.com/manual/reference/command/connPoolSync/

// https://docs.mongodb.com/manual/reference/command/convertToCapped/

// https://docs.mongodb.com/manual/reference/command/create/
class Create final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "create";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        auto sql = nosql::table_create_statement(table(), m_database.config().id_length);

        return sql;
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

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
                    ostringstream ss;
                    ss << "Collection already exists. NS: " << table(Quoted::NO);
                    throw SoftError(ss.str(), error::NAMESPACE_EXISTS);
                }
                else
                {
                    throw MariaDBError(ComERR(response));
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        DocumentBuilder doc;

        doc.append(kvp(key::OK, ok));

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/createIndexes/

// https://docs.mongodb.com/manual/reference/command/currentOp/

// https://docs.mongodb.com/manual/reference/command/drop/
class Drop final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "drop";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        ostringstream sql;

        sql << "DROP TABLE " << table();

        return sql.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

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

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        DocumentBuilder doc;

        doc.append(kvp(key::OK, ok));
        doc.append(kvp(key::NS, table(Quoted::NO)));
        doc.append(kvp(key::N_INDEXES_WAS, 1)); // TODO: Report real value.

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/dropDatabase/
class DropDatabase final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "dropDatabase";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        ostringstream sql;

        sql << "DROP DATABASE `" << m_database.name() << "`";

        return sql.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

        DocumentBuilder doc;
        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            doc.append(kvp(key::DROPPED, m_database.name()));
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_DB_DROP_EXISTS)
                {
                    // Report with "ok" == 1, but without "dropped".
                    ok = 1;
                }
                else
                {
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        doc.append(kvp(key::OK, ok));

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
class KillCursors final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "killCursors";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        string collection = m_database.name() + "." + value_as<string>();
        auto cursors = required<bsoncxx::array::view>("cursors");

        vector<int64_t> ids;

        int i = 0;
        for (const auto& element : cursors)
        {
            if (element.type() != bsoncxx::type::k_int64)
            {
                ostringstream ss;
                ss << "Field 'cursors' contains an element that is not of type long: 0";
                throw SoftError(ss.str(), error::FAILED_TO_PARSE);
            }

            ids.push_back(element.get_int64());
            ++i;
        }

        if (i == 0)
        {
            ostringstream ss;
            ss << "Must specify at least one cursor id in: { killCursors: \"" << value_as<string>()
               << "\" cursors: [], $db: \"" << m_database.name() << "\" }";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        set<int64_t> removed = NoSQLCursor::kill(collection, ids);

        ArrayBuilder cursorsKilled;
        ArrayBuilder cursorsNotFound;
        ArrayBuilder cursorsAlive;
        ArrayBuilder cursorsUnknown;

        for (const auto id : ids)
        {
            if (removed.find(id) != removed.end())
            {
                cursorsKilled.append(id);
            }
            else
            {
                cursorsNotFound.append(id);
            }
        }

        doc.append(kvp(key::CURSORS_KILLED, cursorsKilled.extract()));
        doc.append(kvp(key::CURSORS_NOT_FOUND, cursorsNotFound.extract()));
        doc.append(kvp(key::CURSORS_ALIVE, cursorsAlive.extract()));
        doc.append(kvp(key::CURSORS_UNKNOWN, cursorsUnknown.extract()));
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/manual/reference/command/killOp/

// https://docs.mongodb.com/manual/reference/command/listCollections/
class ListCollections final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "listCollections";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        optional(key::NAME_ONLY, &m_name_only, Conversion::RELAXED);

        bsoncxx::document::view filter;
        if (optional(key::FILTER, &filter))
        {
            MXS_WARNING("listCollections.filter is ignored.");
        }

        ostringstream sql;
        sql << "SHOW TABLES FROM `" << m_database.name() << "`";

        return sql.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

        GWBUF* pResponse = nullptr;

        switch (response.type())
        {
        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_BAD_DB_ERROR)
                {
                    ArrayBuilder firstBatch;
                    pResponse = create_command_response(firstBatch);
                }
                else
                {
                    throw MariaDBError(err);
                }
            }
            break;

        case ComResponse::OK_PACKET:
        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            throw_unexpected_packet();

        default:
            {
                uint8_t* pBuffer = gwbuf_link_data(mariadb_response.get());

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
                    collection.append(kvp(key::NAME, table));
                    collection.append(kvp(key::TYPE, value::COLLECTION));
                    if (!m_name_only)
                    {
                        DocumentBuilder options;
                        DocumentBuilder info;
                        info.append(kvp(key::READ_ONLY, false));
                        //info.append(kvp(key::UUID, ...); // TODO: Could something meaningful be added here?
                        // DocumentBuilder idIndex;
                        // idIndex.append(kvp(key::V, ...));
                        // idIndex.append(kvp(key::KEY, ...));
                        // idIndex.append(kvp(key::NAME, ...));

                        collection.append(kvp(key::OPTIONS, options.extract()));
                        collection.append(kvp(key::INFO, info.extract()));
                        //collection.append(kvp(key::IDINDEX, idIndex.extract()));
                    }

                    firstBatch.append(collection.extract());
                }

                pResponse = create_command_response(firstBatch);
            }
        }

        *ppResponse = pResponse;
        return READY;
    }

private:
    GWBUF* create_command_response(ArrayBuilder& firstBatch)
    {
        string ns = m_database.name() + ".$cmd.listCollections";

        DocumentBuilder cursor;
        cursor.append(kvp(key::ID, int64_t(0)));
        cursor.append(kvp(key::NS, ns));
        cursor.append(kvp(key::FIRST_BATCH, firstBatch.extract()));

        DocumentBuilder doc;
        doc.append(kvp(key::CURSOR, cursor.extract()));
        doc.append(kvp(key::OK, 1));

        return create_response(doc.extract());
    }

    bool m_name_only { false };
};


// https://docs.mongodb.com/manual/reference/command/listDatabases/
class ListDatabases;

template<>
struct IsAdmin<command::ListDatabases>
{
    static const bool is_admin { true };
};

class ListDatabases final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "listDatabases";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    bool is_admin() const override
    {
        return IsAdmin<ListDatabases>::is_admin;
    }

    string generate_sql() override
    {
        optional(key::NAME_ONLY, &m_name_only, Conversion::RELAXED);

        ostringstream sql;
        sql << "SELECT table_schema, table_name, (data_length + index_length) `bytes` "
            << "FROM information_schema.tables "
            << "WHERE table_schema NOT IN ('information_schema', 'performance_schema', 'mysql') "
            << "UNION "
            << "SELECT schema_name as table_schema, '' as table_name, 0 as bytes "
            << "FROM information_schema.schemata "
            << "WHERE schema_name NOT IN ('information_schema', 'performance_schema', 'mysql')";

        return sql.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

        DocumentBuilder doc;

        switch (response.type())
        {
        case ComResponse::ERR_PACKET:
            throw MariaDBError(ComERR(response));
            break;

        case ComResponse::OK_PACKET:
        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            throw_unexpected_packet();

        default:
            {
                uint8_t* pBuffer = gwbuf_link_data(mariadb_response.get());

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
                    database.append(kvp(key::NAME, name));

                    if (!m_name_only)
                    {
                        database.append(kvp(key::SIZE_ON_DISK, bytes));
                        database.append(kvp(key::EMPTY, bytes == 0));
                    }

                    databases.append(database.extract());
                }

                doc.append(kvp(key::DATABASES, databases.extract()));
                doc.append(kvp(key::TOTAL_SIZE, total_size));
                doc.append(kvp(key::OK, 1));
            }
        }

        *ppResponse = create_response(doc.extract());
        return READY;
    }

private:
    bool m_name_only { false };
};

// https://docs.mongodb.com/manual/reference/command/listIndexes/

// https://docs.mongodb.com/manual/reference/command/logRotate/

// https://docs.mongodb.com/manual/reference/command/reIndex/

// https://docs.mongodb.com/manual/reference/command/renameCollection/
class RenameCollection;

template<>
struct IsAdmin<command::RenameCollection>
{
    static const bool is_admin { true };
};

class RenameCollection final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "renameCollection";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    bool is_admin() const override
    {
        return IsAdmin<RenameCollection>::is_admin;
    }

    string generate_sql() override
    {
        require_admin_db();

        m_from = value_as<string>();

        auto i = m_from.find('.');

        if (i == string::npos)
        {
            ostringstream ss;
            ss << "Invalid namespace specified '" << m_from << "'";
            throw SoftError(ss.str(), error::INVALID_NAMESPACE);
        }

        m_to = required<string>("to");

        auto j = m_to.find('.');

        if (j == string::npos)
        {
            ostringstream ss;
            ss << "Invalid target namespace: '" << m_to << "'";
            throw SoftError(ss.str(), error::INVALID_NAMESPACE);
        }

        return "RENAME TABLE " + m_from + " TO " + m_to;
    };

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_NO_SUCH_TABLE:
                    {
                        ostringstream ss;
                        ss << "Source collection " << m_from << " does not exist";
                        throw SoftError(ss.str(), error::NAMESPACE_NOT_FOUND);
                    }
                    break;

                case ER_ERROR_ON_RENAME:
                    {
                        ostringstream ss;
                        ss << "Rename failed, does target database exist?";
                        throw SoftError(ss.str(), error::COMMAND_FAILED);
                    }
                    break;

                case ER_TABLE_EXISTS_ERROR:
                    {
                        throw SoftError("target namespace exists", error::NAMESPACE_EXISTS);
                    }
                    break;

                default:
                    throw MariaDBError(err);
                }
            }
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        DocumentBuilder doc;

        doc.append(kvp(key::OK, ok));

        *ppResponse = create_response(doc.extract());
        return READY;
    }

private:
    string m_from;
    string m_to;
};

// https://docs.mongodb.com/manual/reference/command/setFeatureCompatibilityVersion/

// https://docs.mongodb.com/manual/reference/command/setIndexCommitQuorum/

// https://docs.mongodb.com/manual/reference/command/setParameter/

// https://docs.mongodb.com/manual/reference/command/setDefaultRWConcern/

// https://docs.mongodb.com/manual/reference/command/shutdown/


}

}
