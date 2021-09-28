/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-administration/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

class ManipulateIndexes : public SingleCommand
{
public:
    using SingleCommand::SingleCommand;

    ~ManipulateIndexes()
    {
        if (m_dcid)
        {
            worker().cancel_delayed_call(m_dcid);
        }
    }

    string generate_sql() override final
    {
        ostringstream sql;
        sql << "SELECT 1 FROM " << table() << " LIMIT 0";

        return sql.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        State state = State::READY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (m_action)
        {
        case NORMAL_ACTION:
            state = translate_normal_action(std::move(mariadb_response), &pResponse);
            break;

        case CREATING_TABLE:
            state = translate_creating_table(std::move(mariadb_response), &pResponse);
            break;

        case CREATING_DATABASE:
            state = translate_creating_database(std::move(mariadb_response), &pResponse);
            break;
        }

        *ppResponse = pResponse;

        return state;
    }

protected:
    enum TableAction
    {
        CREATE_IF_MISSING,
        ERROR_IF_MISSING
    };

    void set_table_action(TableAction table_action)
    {
        m_table_action = table_action;
    }

    virtual GWBUF* handle_error(const ComERR& err)
    {
        if (err.code() == ER_NO_SUCH_TABLE)
        {
            throw SoftError(error_message(), error::NAMESPACE_NOT_FOUND);
        }
        else
        {
            throw MariaDBError(err);
        }

        return nullptr;
    }

    virtual GWBUF* collection_exists(bool created) = 0;

private:
    virtual string error_message() const
    {
        return "ns does not exist: " + table(Quoted::NO);
    }

    State translate_normal_action(mxs::Buffer&& mariadb_response, GWBUF **ppResponse)
    {
        State state = State::READY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
        case ComResponse::LOCAL_INFILE_PACKET:
            throw_unexpected_packet();
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (m_table_action == CREATE_IF_MISSING && err.code() == ER_NO_SUCH_TABLE)
                {
                    if (m_database.config().auto_create_tables)
                    {
                        create_table();
                        state = State::BUSY;
                    }
                    else
                    {
                        ostringstream ss;
                        ss << "Table " << table() << " does not exist, and 'auto_create_tables' "
                           << "is false.";

                        throw HardError(ss.str(), error::COMMAND_FAILED);
                    }
                }
                else
                {
                    pResponse = handle_error(err);
                }
            }
            break;

        default:
            pResponse = collection_exists(false);
        }

        *ppResponse = pResponse;
        return state;
    }

    State translate_creating_table(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_action == Action::CREATING_TABLE);

        State state = State::BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            pResponse = collection_exists(true);
            state = State::READY;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_BAD_DB_ERROR:
                    {
                        if (err.message().find("Unknown database") == 0)
                        {
                            if (m_database.config().auto_create_databases)
                            {
                                create_database();
                            }
                            else
                            {
                                ostringstream ss;
                                ss << "Database " << m_database.name() << " does not exist, and "
                                   << "'auto_create_databases' is false.";

                                throw HardError(ss.str(), error::COMMAND_FAILED);
                            }
                        }
                        else
                        {
                            throw MariaDBError(err);
                        }
                    }
                    break;

                case ER_TABLE_EXISTS_ERROR:
                    // Someone created it before we did.
                    pResponse = collection_exists(false);
                    state = State::READY;
                    break;

                default:
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppResponse = pResponse;
        return state;
    }

    State translate_creating_database(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_action == Action::CREATING_DATABASE);

        State state = State::BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            create_table();
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case  ER_DB_CREATE_EXISTS:
                    // Someone else has created the database.
                    create_table();
                    break;

                default:
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppResponse = pResponse;
        return state;
    }

    void create_database()
    {
        mxb_assert(m_action == Action::CREATING_TABLE);
        m_action = Action::CREATING_DATABASE;

        mxb_assert(m_dcid == 0);
        m_dcid = worker().delayed_call(0, [this](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    ostringstream ss;
                    ss << "CREATE DATABASE `" << m_database.name() << "`";

                    send_downstream(ss.str());
                }

                return false;
            });
    }

    void create_table()
    {
        mxb_assert(m_action != Action::CREATING_TABLE);
        m_action = Action::CREATING_TABLE;

        mxb_assert(m_dcid == 0);
        m_dcid = worker().delayed_call(0, [this](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    auto statement = nosql::table_create_statement(table(), m_database.config().id_length);

                    send_downstream(statement);
                }

                return false;
            });
    }

    enum Action
    {
        NORMAL_ACTION,
        CREATING_TABLE,
        CREATING_DATABASE
    };

    TableAction m_table_action { TableAction::ERROR_IF_MISSING };
    Action      m_action       { NORMAL_ACTION };
    uint32_t    m_dcid         { 0 };
};


// https://docs.mongodb.com/v4.4/reference/command/cloneCollectionAsCapped/

// https://docs.mongodb.com/v4.4/reference/command/collMod/

// https://docs.mongodb.com/v4.4/reference/command/compact/

// https://docs.mongodb.com/v4.4/reference/command/connPoolSync/

// https://docs.mongodb.com/v4.4/reference/command/convertToCapped/

// https://docs.mongodb.com/v4.4/reference/command/create/
class Create final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "create";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    using Worker = mxb::Worker;

    ~Create()
    {
        if (m_dcid)
        {
            worker().cancel_delayed_call(m_dcid);
        }
    }

    string generate_sql() override
    {
        bsoncxx::document::view storage_engine;
        if (optional(key::STORAGE_ENGINE, &storage_engine))
        {
            // TODO: Not checked.
        }

        m_statement = nosql::table_create_statement(table(), m_database.config().id_length);

        return m_statement;
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        State state = State::BUSY;
        GWBUF* pResponse = nullptr;

        switch (m_action)
        {
        case Action::CREATING_TABLE:
            state = translate_creating_table(std::move(mariadb_response), &pResponse);
            break;

        case Action::CREATING_DATABASE:
            state = translate_creating_database(std::move(mariadb_response), &pResponse);
            break;
        }

        *ppResponse = pResponse;
        return state;
    }

    State translate_creating_table(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_action == Action::CREATING_TABLE);

        State state = State::BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            {
                DocumentBuilder doc;
                doc.append(kvp(key::OK, (int32_t)1));

                pResponse = create_response(doc.extract());
                state = State::READY;
            }
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_BAD_DB_ERROR:
                    {
                        if (err.message().find("Unknown database") == 0)
                        {
                            if (m_database.config().auto_create_databases)
                            {
                                create_database();
                            }
                            else
                            {
                                ostringstream ss;
                                ss << "Database " << m_database.name() << " does not exist, and "
                                   << "'auto_create_databases' is false.";

                                throw HardError(ss.str(), error::COMMAND_FAILED);
                            }
                        }
                        else
                        {
                            throw MariaDBError(err);
                        }
                    }
                    break;

                case ER_TABLE_EXISTS_ERROR:
                    {
                        ostringstream ss;
                        ss << "Collection already exists. NS: " << table(Quoted::NO);
                        throw SoftError(ss.str(), error::NAMESPACE_EXISTS);
                    }
                    break;

                default:
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppResponse = pResponse;
        return state;
    }

    State translate_creating_database(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_action == Action::CREATING_DATABASE);

        State state = State::BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            create_table();
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case  ER_DB_CREATE_EXISTS:
                    // Someone else has created the database.
                    create_table();
                    break;

                default:
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppResponse = pResponse;
        return state;
    }

    void create_database()
    {
        mxb_assert(m_action == Action::CREATING_TABLE);
        m_action = Action::CREATING_DATABASE;

        mxb_assert(m_dcid == 0);
        m_dcid = worker().delayed_call(0, [this](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    ostringstream ss;
                    ss << "CREATE DATABASE `" << m_database.name() << "`";

                    send_downstream(ss.str());
                }

                return false;
            });
    }

    void create_table()
    {
        mxb_assert(m_action == Action::CREATING_DATABASE);
        m_action = Action::CREATING_TABLE;

        mxb_assert(m_dcid == 0);
        m_dcid = worker().delayed_call(0, [this](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    send_downstream(m_statement);
                }

                return false;
            });
    }

private:
    enum class Action
    {
        CREATING_TABLE,
        CREATING_DATABASE
    };

    Action   m_action { Action::CREATING_TABLE };
    string   m_statement;
    uint32_t m_dcid { 0 };
};


// https://docs.mongodb.com/v4.4/reference/command/createIndexes/
class CreateIndexes final : public ManipulateIndexes
{
public:
    static constexpr const char* const KEY = "createIndexes";
    static constexpr const char* const HELP = "";

    using ManipulateIndexes::ManipulateIndexes;

private:
    void prepare() override
    {
        set_table_action(TableAction::CREATE_IF_MISSING);

        auto indexes = required<bsoncxx::array::view>(key::INDEXES);

        int nIndexes = 0;

        for (const auto& element : indexes)
        {
            ++nIndexes;

            if (element.type() != bsoncxx::type::k_document)
            {
                ostringstream ss;
                ss << "The elements of the 'indexes' array must be objects, but got "
                   << bsoncxx::to_string(element.type());

                throw SoftError(ss.str(), error::TYPE_MISMATCH);
            }

            auto index = static_cast<bsoncxx::document::view>(element.get_document());

            auto key = index[key::KEY];
            if (!key)
            {
                ostringstream ss;
                ss << "Error in specification " << bsoncxx::to_json(index)
                   << " :: caused by :: The 'key' field is a required "
                   << "property of an index specification";

                throw SoftError(ss.str(), error::FAILED_TO_PARSE);
            }

            if (key.type() != bsoncxx::type::k_document)
            {
                ostringstream ss;
                ss << "Error in specification " << bsoncxx::to_json(index)
                   << " :: caused by :: The field 'key' must be an object, but got "
                   << bsoncxx::to_string(key.type());

                throw SoftError(ss.str(), error::TYPE_MISMATCH);
            }
            else
            {
                auto doc = static_cast<bsoncxx::document::view>(key.get_document());

                for (const auto& element : static_cast<bsoncxx::document::view>(key.get_document()))
                {
                    int64_t number;
                    if (nosql::get_number_as_integer(element, &number))
                    {
                        if (number == 0)
                        {
                            ostringstream ss;
                            ss << "Error in specification " << bsoncxx::to_json(doc)
                               << " :: caused by :: Values in the index key pattern cannot be 0.";

                            throw (ss.str(), error::CANNOT_CREATE_INDEX);
                        }
                    }
                    else if (element.type() != bsoncxx::type::k_utf8)
                    {
                        ostringstream ss;
                        ss << "Error in specification " << bsoncxx::to_json(doc)
                           << " :: caused by :: Values in v:2 index key pattern cannot be of type "
                           << bsoncxx::to_string(element.type())
                           << ". Only numbers > 0, numbers < 0, and strings are allowed.";

                        throw SoftError(ss.str(), error::CANNOT_CREATE_INDEX);
                    }
                    else
                    {
                        // We know of no plugins.
                        ostringstream ss;
                        ss << "Error in specification " << bsoncxx::to_json(doc)
                           << " :: caused by :: Unknown index plugin '"
                           << static_cast<string_view>(element.get_utf8())
                           << "'";

                        throw SoftError(ss.str(), error::CANNOT_CREATE_INDEX);
                    }
                }
            }

            auto name = index[key::NAME];
            if (!name)
            {
                ostringstream ss;
                ss << "Error in specification " << bsoncxx::to_json(index)
                   << " :: caused by :: The 'name' field is a required "
                   << "property of an index specification";

                throw SoftError(ss.str(), error::FAILED_TO_PARSE);
            }

            if (name.type() != bsoncxx::type::k_utf8)
            {
                ostringstream ss;
                ss << "Error in specification " << bsoncxx::to_json(index)
                   << " :: caused by :: The field 'name' must be a string, but got "
                   << bsoncxx::to_string(name.type());

                throw SoftError(ss.str(), error::FAILED_TO_PARSE);
            }

            if (static_cast<string_view>(name.get_utf8()).compare("_id_") == 0)
            {
                if (!is_valid_key_for_id(key.get_document()))
                {
                    ostringstream ss;
                    ss << "The index name '_id_' is reserved for the _id index, "
                       << "which must have key pattern {_id: 1}, found key: "
                       << bsoncxx::to_json(key.get_document());

                    throw SoftError(ss.str(), error::BAD_VALUE);
                }
            }
        }

        if (nIndexes == 0)
        {
            ostringstream ss;
            ss << "Must specify at least on index to create";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }
    }

    GWBUF* collection_exists(bool created) override
    {
        return report_success(created);
    }

    static bool is_valid_key_for_id(const bsoncxx::document::view& key)
    {
        for (const auto& field : key)
        {
            if (field.key().compare("_id") != 0)
            {
                return false;
            }

            int64_t v;
            if (!nosql::get_number_as_integer(field, &v))
            {
                return false;
            }

            if (v != 1)
            {
                return false;
            }
        }

        return true;
    }

    GWBUF* report_success(bool created)
    {
        MXS_WARNING("Unsupported command '%s' used, claiming success.", name().c_str());

        DocumentBuilder doc;
        doc.append(kvp(key::CREATED_COLLECTION_AUTOMATICALLY, created));
        doc.append(kvp(key::OK, 1));

        return create_response(doc.extract());
    }
};

// https://docs.mongodb.com/v4.4/reference/command/currentOp/
class CurrentOp;

template<>
struct IsAdmin<command::CurrentOp>
{
    static const bool is_admin { true };
};

class CurrentOp : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "currentOp";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    bool is_admin() const override
    {
        return IsAdmin<CurrentOp>::is_admin;
    }

    void populate_response(DocumentBuilder& doc) override
    {
        ArrayBuilder inprog;
        // TODO: Add something.

        doc.append(kvp(key::INPROG, inprog.extract()));
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/drop/
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
            NoSQLCursor::purge(table(Quoted::NO));
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
        return State::READY;
    }
};


// https://docs.mongodb.com/v4.4/reference/command/dropDatabase/
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
        return State::READY;
    }
};


// https://docs.mongodb.com/v4.4/reference/command/dropConnections/

// https://docs.mongodb.com/v4.4/reference/command/dropIndexes/
class DropIndexes final : public ManipulateIndexes
{
public:
    static constexpr const char* const KEY = "dropIndexes";
    static constexpr const char* const HELP = "";

    using ManipulateIndexes::ManipulateIndexes;

private:
    string error_message() const override
    {
        return "ns not found " + table(Quoted::NO);
    }

    GWBUF* collection_exists(bool created) override
    {
        int32_t nIndexes_was = 1;

        auto element = m_doc[key::INDEX];

        if (element)
        {
            switch (element.type())
            {
            case bsoncxx::type::k_array:
                {
                    auto indexes = static_cast<bsoncxx::array::view>(element.get_array());

                    for (const auto& index : indexes)
                    {
                        if (index.type() == bsoncxx::type::k_utf8)
                        {
                            check_index(index.get_utf8());
                            // If a specific index was named, we assume the client knew what
                            // it was doing and return 2. Namely, as the index _id_ always
                            // exists, if there were additional indexes, there must at least
                            // have been 2.
                            nIndexes_was = 2;
                        }
                    }
                }
                break;

            case bsoncxx::type::k_utf8:
                {
                    check_index(element.get_utf8());;
                    nIndexes_was = 2; // See above.
                }
                break;

            default:
                break;
            }
        }

        MXS_WARNING("Unsupported command '%s' used, claiming success.", name().c_str());

        DocumentBuilder doc;
        doc.append(kvp("nIndexesWas", nIndexes_was));
        doc.append(kvp("ok", (int32_t)1));

        return create_response(doc.extract());
    }

private:
    void check_index(const string_view& s)
    {
        if (s.compare("_id_") == 0)
        {
            ostringstream ss;
            ss << "cannot drop _id index";

            throw SoftError(ss.str(), error::INVALID_OPTIONS);
        }
    }
};

// https://docs.mongodb.com/v4.4/reference/command/filemd5/

// https://docs.mongodb.com/v4.4/reference/command/fsync/

// https://docs.mongodb.com/v4.4/reference/command/fsyncUnlock/

// https://docs.mongodb.com/v4.4/reference/command/getDefaultRWConcern/

// https://docs.mongodb.com/v4.4/reference/command/getParameter/

// https://docs.mongodb.com/v4.4/reference/command/killCursors/
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

// https://docs.mongodb.com/v4.4/reference/command/killOp/

// https://docs.mongodb.com/v4.4/reference/command/listCollections/
class ListCollections final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "listCollections";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        optional(key::NAME_ONLY, &m_name_only, Conversion::RELAXED);

        string suffix;

        bsoncxx::document::view filter;
        if (optional(key::FILTER, &filter))
        {
            for (const auto& element : filter)
            {
                if (element.key().compare(key::NAME) == 0)
                {
                    string command(KEY);
                    command += ".filter";

                    suffix = " LIKE \"" + element_as<string>(command, key::NAME, element) + "\"";
                }
                else
                {
                    string name(element.key().data(), element.key().length());
                    MXS_WARNING("listCollections.filter.%s is not supported.", name.c_str());
                }
            }
        }

        ostringstream sql;
        sql << "SHOW TABLES FROM `" << m_database.name() << "`" << suffix;

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
        return State::READY;
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


// https://docs.mongodb.com/v4.4/reference/command/listDatabases/
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
                if (!m_name_only)
                {
                    doc.append(kvp(key::TOTAL_SIZE, total_size));
                }
                doc.append(kvp(key::OK, 1));
            }
        }

        *ppResponse = create_response(doc.extract());
        return State::READY;
    }

private:
    bool m_name_only { false };
};

// https://docs.mongodb.com/v4.4/reference/command/listIndexes/
class ListIndexes final : public ManipulateIndexes
{
public:
    static constexpr const char* const KEY = "listIndexes";
    static constexpr const char* const HELP = "";

    using ManipulateIndexes::ManipulateIndexes;

private:
    GWBUF* collection_exists(bool created) override
    {
        DocumentBuilder key;
        key.append(kvp(key::_ID, (int32_t)1));

        DocumentBuilder index;
        index.append(kvp(key::V, (int32_t)2)); // TODO: What is this?
        index.append(kvp(key::KEY, key.extract()));
        index.append(kvp(key::NAME, key::_ID_));

        ArrayBuilder first_batch;
        first_batch.append(index.extract());

        DocumentBuilder cursor;
        cursor.append(kvp(key::ID, (int64_t)0));
        cursor.append(kvp(key::NS, table(Quoted::NO)));
        cursor.append(kvp(key::FIRST_BATCH, first_batch.extract()));

        DocumentBuilder doc;
        doc.append(kvp(key::CURSOR, cursor.extract()));
        doc.append(kvp(key::OK, (int32_t)1));

        return create_response(doc.extract());
    }
};

// https://docs.mongodb.com/v4.4/reference/command/logRotate/

// https://docs.mongodb.com/v4.4/reference/command/reIndex/

// https://docs.mongodb.com/v4.4/reference/command/renameCollection/
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
        return State::READY;
    }

private:
    string m_from;
    string m_to;
};

// https://docs.mongodb.com/v4.4/reference/command/setFeatureCompatibilityVersion/

// https://docs.mongodb.com/v4.4/reference/command/setIndexCommitQuorum/

// https://docs.mongodb.com/v4.4/reference/command/setParameter/

// https://docs.mongodb.com/v4.4/reference/command/setDefaultRWConcern/

// https://docs.mongodb.com/v4.4/reference/command/shutdown/


}

}
