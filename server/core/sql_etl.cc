/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/sql_etl.hh"
#include "internal/sql_etl_generic.hh"
#include "internal/servermanager.hh"
#include <maxbase/format.hh>

namespace
{
using Type = mxb::Json::Type;
using namespace sql_etl;

// Counts the number of rows in a resultset and updates a variable
class RowCountObserver : public mxq::Output
{
public:
    RowCountObserver(mxq::Output* output, int64_t& ref)
        : m_output(output)
        , m_ref(ref)
    {
    }

    bool ok_result(int64_t rows_affected, int64_t warnings) override
    {
        return m_output->ok_result(rows_affected, warnings);
    }

    bool resultset_start(const std::vector<mxq::ColumnInfo>& metadata) override
    {
        return m_output->resultset_start(metadata);
    }

    bool resultset_rows(const std::vector<mxq::ColumnInfo>& metadata,
                        mxq::ResultBuffer& res,
                        uint64_t rows_fetched) override
    {
        m_ref += rows_fetched;
        return m_output->resultset_rows(metadata, res, rows_fetched);
    }

    bool resultset_end(bool ok, bool complete) override
    {
        return m_output->resultset_end(ok, complete);
    }

    bool error_result(int errnum, const std::string& errmsg, const std::string& sqlstate) override
    {
        return m_output->error_result(errnum, errmsg, sqlstate);
    }

private:
    mxq::Output* m_output;
    int64_t&     m_ref;
};

mxb::Json maybe_get(const mxb::Json& json, const std::string& path, mxb::Json::Type type)
{
    auto elem = json.at(path);
    return elem.type() == type ? elem : mxb::Json(type);
}

mxb::Json get(const mxb::Json& json, const std::string& path, mxb::Json::Type type)
{
    auto elem = json.at(path);

    if (elem.type() != type)
    {
        throw problem("Value at '", path, "' is '", elem.type(), "', expected '", type, "'");
    }
    else if (type == mxb::Json::Type::STRING && json_string_length(elem.get_json()) == 0)
    {
        throw problem("Value at '", path, "' is an empty string");
    }

    return elem;
}

std::string field_from_result(mxq::ODBC& source, const std::string& sql, int field)
{
    std::string rval;
    mxq::TextResult textresult;

    if (source.query(sql, &textresult))
    {
        if (auto val = textresult.get_field(field))
        {
            rval = *val;
        }
        else
        {
            mxb_assert_message(!true, "Query did not return a result: %s", sql.c_str());
            throw problem("Unexpected empty result");
        }
    }
    else
    {
        throw problem(source.error());
    }

    return rval;
}

const char* stage_to_str(sql_etl::ETL::Stage stage)
{
    switch (stage)
    {
    case sql_etl::ETL::Stage::PREPARE:
        return "prepare";

    case sql_etl::ETL::Stage::CREATE:
        return "create";

    case sql_etl::ETL::Stage::LOAD:
        return "load";
    }

    mxb_assert(!true);
    return "unknown";
}
}

namespace sql_etl
{
class MariaDBExtractor final : public Extractor
{
public:
    void init_connection(mxq::ODBC& source) override
    {
        if (!source.query("SET AUTOCOMMIT=0,SQL_MODE='PIPES_AS_CONCAT,NO_ENGINE_SUBSTITUTION'"))
        {
            throw problem(source.error());
        }
    }

    void start(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
        std::ostringstream ss;
        ss << "LOCK TABLE "
           << mxb::transform_join(tables, [](const auto& t){
            return "`"s + t.schema() + "`.`" + t.table() + "` READ";
        }, ",")
           << " WAIT " << source.query_timeout().count();

        if (!source.query(ss.str()))
        {
            throw problem(source.error());
        }
    }

    void start_thread(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
        if (!source.query("START TRANSACTION WITH CONSISTENT SNAPSHOT"))
        {
            throw problem(source.error());
        }
    }

    void threads_started(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
        if (!source.query("UNLOCK TABLES"))
        {
            throw problem(source.error());
        }
    }

    std::string create_table(mxq::ODBC& source, const Table& table) override
    {
        std::string sql = mxb::string_printf("SHOW CREATE TABLE `%s`.`%s`", table.schema(), table.table());
        std::string result = field_from_result(source, sql, 1);
        std::string_view original = "CREATE TABLE";
        std::string_view replacement = to_create_table(table.create_mode());

        if (replacement != original)
        {
            if (auto pos = result.find(original); pos != std::string::npos)
            {
                result.replace(pos, original.size(), replacement);
            }
            else
            {
                throw problem("Malformed response to `SHOW CREATE TABLE`: ", result);
            }
        }

        return result;
    }

    std::string select(mxq::ODBC& source, const Table& table) override
    {
        const char* format =
            R"(
SELECT
  'SELECT ' || GROUP_CONCAT('`' || COLUMN_NAME || '`' ORDER BY ORDINAL_POSITION SEPARATOR ',') ||
  ' FROM `' || TABLE_SCHEMA || '`.`' || TABLE_NAME || '`'
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = '%s' AND TABLE_NAME = '%s' AND IS_GENERATED = 'NEVER'
GROUP BY TABLE_SCHEMA, TABLE_NAME;
)";

        std::string sql = mxb::string_printf(format, table.schema(), table.table());
        return field_from_result(source, sql, 0);
    }

    std::string insert(mxq::ODBC& source, const Table& table) override
    {
        const char* format =
            R"(
SELECT
  'INSERT INTO `' || TABLE_SCHEMA || '`.`' || TABLE_NAME ||
  '` (' || GROUP_CONCAT('`' || COLUMN_NAME || '`' ORDER BY ORDINAL_POSITION SEPARATOR ',') ||
  ') VALUES (' || GROUP_CONCAT('?' SEPARATOR ',') || ')'
FROM INFORMATION_SCHEMA.COLUMNS
WHERE TABLE_SCHEMA = '%s' AND TABLE_NAME = '%s' AND IS_GENERATED = 'NEVER'
GROUP BY TABLE_SCHEMA, TABLE_NAME;
)";

        std::string sql = mxb::string_printf(format, table.schema(), table.table());
        return field_from_result(source, sql, 0);
    }
};

class PostgresqlExtractor final : public Extractor
{
public:
    void init_connection(mxq::ODBC& source) override
    {
        for (auto query : {
                "SET DATESTYLE = ISO",
                "SET INTERVALSTYLE = SQL_STANDARD",
                "SET statement_timeout = 0",
                "SET idle_in_transaction_session_timeout = 0",
                "SET lock_timeout = 0",
                "SET extra_float_digits = 3",
                "SET client_encoding = UTF8",
                "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"
            })
        {
            if (!source.query(query))
            {
                MXB_INFO("Query '%s' failed: %d, %s", query, source.errnum(), source.error().c_str());
                throw problem("Failed to prepare connection: ", source.error());
            }
        }
    }

    void start(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
        for (const auto& t : tables)
        {
            auto sql = mxb::string_printf(R"(LOCK TABLE "%s"."%s" IN ACCESS SHARE MODE)",
                                          t.schema(), t.table());

            if (!source.query(sql))
            {
                throw problem("Failed to lock table `", t.schema(), "`.`", t.table(), "`: ", source.error());
            }
        }

        mxq::TextResult textresult;
        if (source.query("SELECT pg_export_snapshot()", &textresult))
        {
            if (auto val = textresult.get_field(0))
            {
                m_snapshot = *val;
            }
            else
            {
                throw problem("Transaction snapshot was null");
            }
        }
        else
        {
            throw problem("Failed to retrieve transaction snapshot: ", source.error());
        }
    }

    void start_thread(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
        // Taking a shared lock on the tables prevents them from being deleted or modified while the
        // transaction is ongoing. The NOWAIT option is what prevents deadlocks from happening: if an outside
        // connection manages to request an exclusive lock after we get the initial shared locks but before
        // all of the threads have acquired their own locks, they would be blocked by the exclusive lock which
        // in turn would be blocked by the initial shared locks.
        for (const auto& t : tables)
        {
            auto sql = mxb::string_printf(R"(LOCK TABLE "%s"."%s" IN ACCESS SHARE MODE NOWAIT)",
                                          t.schema(), t.table());

            if (!source.query(sql))
            {
                throw problem("Locking conflict for table `", t.schema(), "`.`", t.table(), "`",
                              ", cannot proceed: ", source.error());
            }
        }

        if (!source.query("SET TRANSACTION SNAPSHOT '" + m_snapshot + "'"))
        {
            throw problem("Failed to import transaction snapshot: ", source.error());
        }
    }

    void threads_started(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
        // Tables are unlocked when the transaction ends
    }

    std::string create_table(mxq::ODBC& source, const Table& table) override
    {
        /**
         * The various PostgreSQL types are converted as follows:
         *
         * - If a.attndims is larger than zero, then the field is an array and it is converted into a JSON
         *   array
         *
         * - The hstore key-value type is converted into a JSON object
         *
         * - JSONB is converted into JSON, MariaDB doesn't have a binary type
         *
         * - The PostgreSQL-only geometry types 'line', 'lseg', 'box' and 'circle' are converted to TEXT
         *
         * - The 'geometry' type is converted into a plain GEOMETRY type, Postgres otherwise declares it as a
         *   non-standard type.
         *
         * - The INET type is converted into INET6 as PostgreSQL supports a mix of IPv4 and IPv6 addresses.
         *
         * - All PostgreSQL SERIAL types are converted into MariaDB AUTO_INCREMENT fields. This isn't a 100%
         *   compatible mapping as PostgreSQL uses a SEQUENCE to implement it which allows multiple SERIAL
         *   fields to be defined and they do not need to be a part of the PRIMARY KEY to work. MariaDB
         *   requires that there is only one AUTO_INCREMENT field and it must be a part of the primary key.
         *
         * - Fields with with 'NULL::' as the starting of the expression mean that the field has a default
         *   value of NULL. This usually seems to happen when an explicit NULL default is used instead of an
         *   implicit one.
         *
         * - Fields that use a sequence for their default value are converted to use the MariaDB syntax.
         *
         * - If a.attgenerated is an empty string, then the field has a default value. Otherwise, the field is
         *   a generated column.
         *
         * - CHECK constrains are extracted as-is which means they must be compatible with MariaDB.
         *
         * - The internal PostgreSQL OID and XID types convert to BIGINT
         *
         * - The name and pg_node_tree types are converted into text
         *
         * - The internal "char" type is converted to an actual CHAR(1) type
         *
         * - Composite and user-defined types are converted into JSON
         */
        std::ostringstream ss;
        ss <<
            R"(
SELECT '`' || a.attname || '` ' ||
  CASE
    WHEN a.attndims > 0 THEN 'JSON'
    WHEN t.typname IN ('jsonb', 'json', 'hstore') OR t.typtype = 'c' THEN 'JSON'
    WHEN t.typname LIKE 'timestamp%' THEN 'DATETIME(6)'
    WHEN t.typname LIKE 'time%' THEN 'TIME'
    WHEN t.typname IN ('line', 'lseg', 'box', 'circle', 'cidr', 'macaddr', 'macaddr8', 'name', 'pg_node_tree') THEN 'TEXT'
    WHEN t.typname = 'geometry' THEN 'GEOMETRY'
    WHEN t.typname = 'inet' THEN 'INET6'
    WHEN t.typname = 'bytea' THEN 'LONGBLOB'
    WHEN t.typname = 'xml' THEN 'LONGTEXT'
    WHEN t.typname IN ('oid', 'xid') THEN 'BIGINT'
    WHEN UPPER(pg_catalog.format_type(a.atttypid, a.atttypmod)) = '"CHAR"' THEN 'CHAR(1)'
    ELSE UPPER(pg_catalog.format_type(a.atttypid, a.atttypmod))
  END ||
  CASE WHEN a.attnotnull THEN ' NOT NULL' ELSE '' END ||
  CASE
    WHEN pg_catalog.pg_get_serial_sequence(QUOTE_IDENT(n.nspname) || '.' || QUOTE_IDENT(c.relname), a.attname) IS NOT NULL
      THEN ' AUTO_INCREMENT'
    WHEN pg_catalog.pg_get_expr(d.adbin, d.adrelid, true) LIKE 'NULL::%'
      THEN ' DEFAULT NULL'
    WHEN pg_catalog.pg_get_expr(d.adbin, d.adrelid, true) LIKE 'nextval(%'
      THEN ' DEFAULT ' || TRANSLATE(REPLACE(pg_catalog.pg_get_expr(d.adbin, d.adrelid, true), '::regclass', ''), '''', '')
    ELSE
      COALESCE(CASE WHEN a.attgenerated = '' THEN ' DEFAULT ' ELSE ' AS ' END || '(' || pg_catalog.pg_get_expr(d.adbin, d.adrelid, true) || ')', '')
  END  ||
  COALESCE(' ' || pg_catalog.pg_get_constraintdef(ct.oid), '')
  colcol
FROM pg_class c
  JOIN pg_namespace n ON (n.oid = c.relnamespace)
  JOIN pg_attribute a ON (a.attrelid = c.oid)
  JOIN pg_type t ON (t.oid = a.atttypid)
  LEFT JOIN pg_attrdef d ON (d.adrelid = a.attrelid AND d.adnum = a.attnum)
  LEFT JOIN pg_constraint ct ON (ct.conrelid = c.oid AND a.attnum = ANY(ct.conkey) AND ct.contype = 'c')
WHERE a.attnum > 0
AND n.nspname = ')" << table.schema() << R"('
AND c.relname = ')" << table.table() << R"('
ORDER BY a.attnum
)";
        std::string col_sql = ss.str();
        ss.str("");

        /**
         * PostgreSQL has many index types and the ones that MariaDB support are primary keys, unique keys,
         * normal indexes and spatial indexes. There doesn't seem to be built-in fulltext indexes but there
         * are some contributed modules. For now, ignore those and let the user deal with those.
         */
        ss <<
            R"(
SELECT
  CASE
    WHEN BOOL_OR(ix.indisprimary) THEN 'PRIMARY KEY'
    WHEN BOOL_OR(ix.indisunique) THEN 'UNIQUE KEY `' || i.relname || '`'
    ELSE 'KEY `' || i.relname || '`'
  END
  || '(' || STRING_AGG('`' || a.attname || '`', ', ' ORDER BY array_positions(ix.indkey, a.attnum)) || ')' idx
FROM pg_class t, pg_class i, pg_index ix, pg_attribute a, pg_namespace n
WHERE
  t.oid = ix.indrelid
  AND i.oid = ix.indexrelid
  AND n.oid = t.relnamespace
  AND a.attrelid = t.oid
  AND a.attnum = ANY(ix.indkey)
  AND t.relkind = 'r'
  AND n.nspname = ')" << table.schema() << R"('
  AND t.relname = ')" << table.table() << R"('
GROUP BY i.relname, t.relname
)";
        std::string idx_sql = ss.str();
        ss.str("");

        /**
         * PostgreSQL has a slightly different syntax when it comes to declaring foreign keys. They are of the
         * form `[CONSTRAINT name] FOREIGN KEY (fk_columns) REFERENCES (pk_columns)` and the output of
         * pg_get_constraintdef() never seems to contain the constraint name. We can map these into the
         * MariaDB form by manually adding the constraint and index names into the foreign key definition.
         * This is much easier than having to deal with the pg_constraint table and the arrays it uses
         * to define the field order.
         */
        ss <<
            R"(
SELECT 'CONSTRAINT `' || ct.conname || '`' ||
' FOREIGN KEY `' || (SELECT relname FROM pg_class WHERE oid = ct.conindid) || '` ' ||
REPLACE(pg_catalog.pg_get_constraintdef(ct.oid), 'FOREIGN KEY (', '(')
FROM pg_class t JOIN pg_constraint ct ON (t.oid = ct.conrelid)
JOIN pg_namespace n ON (t.relnamespace = n.oid)
WHERE
  ct.contype = 'f'
  AND n.nspname = ')" << table.schema() << R"('
  AND t.relname = ')" << table.table() << R"('
)";
        std::string fk_sql = ss.str();

        // Processing the results separately avoids the need to use CTEs and STRING_AGG to combine the fields.
        // It also allows us to format the result to look similar to SHOW CREATE TABLE.
        std::vector<std::string> values;

        for (auto sql : {col_sql, idx_sql, fk_sql})
        {
            mxq::TextResult textresult;

            if (source.query(sql, &textresult))
            {
                if (textresult.result().size() != 1)
                {
                    mxb_assert_message(!true, "Wrong number of results (%lu): %s",
                                       textresult.result().size(), sql.c_str());
                    throw problem("Unexpected number of results");
                }

                for (const auto& row : textresult.result()[0])
                {
                    if (row.size() != 1 || !row[0])
                    {
                        mxb_assert_message(!true, "Wrong number of results (%lu) or null row", row.size());
                        throw problem("Unexpected result value");
                    }

                    values.push_back(*row[0]);
                }
            }
            else
            {
                throw problem(source.error());
            }
        }

        std::string_view create_table = to_create_table(table.create_mode());

        ss.str("");
        ss << create_table << " `" << table.schema() << "`.`" << table.table() << "`(\n  ";
        ss << mxb::join(values, ",\n  ");
        ss << "\n)";
        return ss.str();
    }

    std::string select(mxq::ODBC& source, const Table& table) override
    {
        /**
         * The SELECT statement must also be generated based on the table layout and possibly also on the data
         * itself. The generated SQL is minimally formatted into a somewhat readable form. The following data
         * type conversions are done on the PostgreSQL side:
         *
         * - Geometry types are extracted into their WKT form, the native display format is something else.
         *
         * - hstore and array are read in their JSON form.
         *
         * - As the inet type in PostgreSQL is a mix of IPv4 and IPv6 addresses, we need to map IPv4 addresses
         *   into the IPv6 form. The inet type also stores a netmask which must be stripped off as MariaDB
         *   doesn't support them.
         */
        const char* format =
            R"(
SELECT
  E'SELECT\n' ||
  STRING_AGG(
    '  ' ||
    CASE
    WHEN LOWER(data_type) IN ('point', 'path', 'polygon') OR LOWER(udt_name) IN ('geometry')
      THEN 'ST_AsText(CAST(' || QUOTE_IDENT(column_name) || ' AS GEOMETRY)) ' || QUOTE_IDENT(column_name)
    WHEN LOWER(udt_name) IN ('hstore')
      THEN 'hstore_to_json_loose(' || QUOTE_IDENT(column_name) || ') ' || QUOTE_IDENT(column_name)
    WHEN LOWER(data_type) IN ('array', 'user-defined')
      THEN 'to_json(' || QUOTE_IDENT(column_name) || ') ' || QUOTE_IDENT(column_name)
    WHEN LOWER(data_type) = 'inet'
      THEN 'CASE FAMILY(' || QUOTE_IDENT(column_name) || ') WHEN 4 THEN ''::ffff:'' ELSE '''' END || HOST(' || QUOTE_IDENT(column_name) || ') ' || QUOTE_IDENT(column_name)
    ELSE
      QUOTE_IDENT(column_name)
    END
    , E',\n' ORDER BY ordinal_position) ||
  E'\nFROM ' || QUOTE_IDENT(table_schema) || '.' || QUOTE_IDENT(table_name)
FROM information_schema.columns
WHERE table_schema = '%s' AND table_name = '%s' AND is_generated = 'NEVER'
GROUP BY table_schema, table_name;
)";

        std::string sql = mxb::string_printf(format, table.schema(), table.table());
        return field_from_result(source, sql, 0);
    }

    std::string insert(mxq::ODBC& source, const Table& table) override
    {
        const char* format =
            R"(
SELECT
  E'INSERT INTO `%s`.`%s` (\n' ||
  STRING_AGG( '  `' || column_name || '`', E',\n' ORDER BY ordinal_position)
  || E'\n) VALUES (' || STRING_AGG(
    CASE
    WHEN LOWER(data_type) IN ('point', 'path', 'polygon') OR LOWER(udt_name) IN ('geometry')
      THEN 'ST_GeomFromText(?)'
    ELSE
      '?'
    END
    , ', ') || ')'
FROM information_schema.columns
WHERE table_schema = '%s' AND table_name = '%s' AND is_generated = 'NEVER'
GROUP BY table_schema, table_name;
)";

        std::string sql = mxb::string_printf(format,
                                             table.schema(), table.table(),
                                             table.schema(), table.table());

        return field_from_result(source, sql, 0);
    }

private:
    std::string m_snapshot;
};

std::unique_ptr<ETL> create(std::string_view id, const mxb::Json& json,
                            const HttpSql::ConnectionConfig& src_cc,
                            const HttpSql::ConnectionConfig& dest_cc)
{

    auto maybe_add = [](const std::string& keyword, const std::string& str){
        return str.empty() ? "" : keyword + "=" + str + ";";
    };

    if (src_cc.target != "odbc")
    {
        throw problem("Only ODBC targets are supported");
    }

    mxb_assert(ServerManager::find_by_unique_name(dest_cc.target));

    std::ostringstream ss;

    // We know what the library name is and it'll work regardless of the odbc.ini configuration.
    ss << "DRIVER=libmaodbc.so;";

    // If the user provided some extra options, put them first so that they take precedence over the ones
    // that are generated from the server configuration.
    if (auto extra = maybe_get(json, "connection_string", Type::STRING).get_string(); !extra.empty())
    {
        if (extra.back() != ';')
        {
            extra += ';';
        }

        ss << extra;
    }

    uint64_t option = mxq::ODBC::MULTI_STMT // Enables multi-statement SQL.
        | mxq::ODBC::FORWARDONLY            // Forces a forward-only cursor (fixes some legacy problems)
        | mxq::ODBC::NO_CACHE;              // Streams the resultset instead of reading it into memory

    ss << "SERVER=" << dest_cc.host << ";"
       << "PORT=" << dest_cc.port << ";"
       << "UID=" << dest_cc.user << ";"
       << "PWD={" << dest_cc.password << "};"
       << "OPTION=" << option << ";"
       << "CONN_TIMEOUT=" << dest_cc.timeout << ";";

    ss << maybe_add("DATABASE", dest_cc.db);

    if (dest_cc.ssl.enabled)
    {
        ss << maybe_add("SSLCERT", dest_cc.ssl.cert)
           << maybe_add("SSLKEY", dest_cc.ssl.key)
           << maybe_add("SSLCA", dest_cc.ssl.ca)
           << maybe_add("SSLCRL", dest_cc.ssl.crl)
           << maybe_add("SSLCIPHER", dest_cc.ssl.cipher);
    }

    auto type_str = get(json, "type", Type::STRING).get_string();
    std::unique_ptr<Extractor> extractor;

    std::string src = src_cc.odbc_string;

    if (type_str == "mariadb")
    {
        extractor = std::make_unique<MariaDBExtractor>();
    }
    else if (type_str == "postgresql")
    {
        extractor = std::make_unique<PostgresqlExtractor>();
    }
    else if (type_str == "generic")
    {
        extractor = std::make_unique<GenericExtractor>(get(json, "catalog", Type::STRING).get_string());
    }
    else
    {
        throw problem("Unknown value for 'type': ", type_str);
    }

    auto tables = get(json, "tables", Type::ARRAY).get_array_elems();

    if (tables.empty())
    {
        throw problem("No tables defined");
    }

    Config cnf{src, ss.str()};
    cnf.threads = std::min(16UL, tables.size());

    if (int64_t threads = maybe_get(json, "threads", mxb::Json::Type::INTEGER).get_int(); threads > 0)
    {
        cnf.threads = std::min((size_t)threads, tables.size());
    }

    if (int64_t timeout = maybe_get(json, "timeout", mxb::Json::Type::INTEGER).get_int(); timeout > 0)
    {
        cnf.timeout = std::chrono::seconds{timeout};
    }

    if (auto mode = maybe_get(json, "create_mode", mxb::Json::Type::STRING).get_string(); !mode.empty())
    {
        if (mode == "normal")
        {
            cnf.create_mode = Config::CreateMode::NORMAL;
        }
        else if (mode == "ignore")
        {
            cnf.create_mode = Config::CreateMode::IGNORE;
        }
        else if (mode == "replace")
        {
            cnf.create_mode = Config::CreateMode::REPLACE;
        }
        else
        {
            throw problem("Unknown value for 'create_mode': ", mode);
        }
    }

    std::unique_ptr<ETL> etl = std::make_unique<ETL>(id, cnf, std::move(extractor));

    for (const auto& val : tables)
    {
        etl->tables().emplace_back(*etl,
                                   get(val, "schema", Type::STRING).get_string(),
                                   get(val, "table", Type::STRING).get_string(),
                                   maybe_get(val, "create", Type::STRING).get_string(),
                                   maybe_get(val, "select", Type::STRING).get_string(),
                                   maybe_get(val, "insert", Type::STRING).get_string());
    }

    return etl;
}

std::string_view to_create_table(Config::CreateMode mode)
{
    switch (mode)
    {
    case Config::CreateMode::NORMAL:
        return "CREATE TABLE";

    case Config::CreateMode::REPLACE:
        return "CREATE OR REPLACE TABLE";

    case Config::CreateMode::IGNORE:
        return "CREATE TABLE IF NOT EXISTS";
    }

    return "ERROR";
}

Table::Table(ETL& etl,
             std::string_view schema,
             std::string_view table,
             std::string_view create,
             std::string_view select,
             std::string_view insert)
    : m_etl(etl)
    , m_schema(schema)
    , m_table(table)
    , m_create(create)
    , m_select(select)
    , m_insert(insert)
{
}

Config::CreateMode Table::create_mode() const
{
    // TODO: Allow the create mode to also be defined on the table level
    return m_etl.config().create_mode;
}

mxb::Json Table::to_json() const
{
    std::lock_guard guard(m_lock);
    mxb::Json obj(mxb::Json::Type::OBJECT);
    obj.set_string("table", m_table);
    obj.set_string("schema", m_schema);

    if (!m_create.empty())
    {
        obj.set_string("create", m_create);
    }

    if (!m_select.empty())
    {
        obj.set_string("select", m_select);
    }

    if (!m_insert.empty())
    {
        obj.set_string("insert", m_insert);
    }

    if (!m_error.empty())
    {
        obj.set_string("error", m_error);
    }

    if (m_duration.count())
    {
        obj.set_float("execution_time", mxb::to_secs(m_duration));
    }

    if (m_rows > 0)
    {
        obj.set_int("rows", m_rows);
    }

    return obj;
}

void Table::read_sql(mxq::ODBC& source)
{
    try
    {
        std::lock_guard guard(m_lock);
        auto& extractor = m_etl.extractor();

        if (m_create.empty())
        {
            std::string create = extractor.create_table(source, *this);
            m_create = "CREATE DATABASE IF NOT EXISTS `" + m_schema + "`;\n";
            m_create += "USE `" + m_schema + "`;\n";
            m_create += create;
        }

        if (m_select.empty())
        {
            m_select = extractor.select(source, *this);
        }

        if (m_insert.empty())
        {
            m_insert = extractor.insert(source, *this);
        }
    }
    catch (const Error& e)
    {
        m_error = e.what();
        m_etl.add_error();
    }
}

void Table::prepare_sql(mxq::ODBC& source, mxq::ODBC& dest)
{
    if (!source.prepare(m_select))
    {
        throw problem("Failed to prepare SELECT: ", source.error());
    }

    if (!dest.prepare(m_insert))
    {
        throw problem("Failed to prepare INSERT: ", dest.error());
    }

    int source_params = source.num_columns();
    int dest_params = dest.num_params();

    if (source_params >= 0 && dest_params >= 0 && source_params != dest_params)
    {
        throw problem("Column count mismatch: ",
                      "SELECT returns ", source_params, " columns but ",
                      "INSERT takes ", dest_params, " parameters.");
    }
}

void Table::create_objects(mxq::ODBC& source, mxq::ODBC& dest)
{
    try
    {
        mxb_assert(!m_create.empty());

        mxq::NoResult res;

        if (!dest.query(m_create, &res) || !res.ok())
        {
            throw problem("Failed to create table: ", dest.error());
        }

        prepare_sql(source, dest);
        source.unprepare();
        dest.unprepare();
    }
    catch (const Error& e)
    {
        m_error = e.what();
        m_etl.add_error();
    }
}

void Table::load_data(mxq::ODBC& source, mxq::ODBC& dest)
{
    try
    {
        mxb_assert(!m_select.empty() && !m_insert.empty());
        auto start = mxb::Clock::now();
        prepare_sql(source, dest);

        RowCountObserver observer(dest.as_output(), m_rows);

        if (!source.execute(&observer))
        {
            const char* who = !source.error().empty() ? "Source: " : "Destination: ";
            throw problem("Failed to load data. ", who, source.error(), dest.error());
        }

        auto end = mxb::Clock::now();
        m_duration = end - start;
    }
    catch (const Error& e)
    {
        MXB_INFO("%s", e.what());
        m_error = e.what();
        m_etl.add_error();
    }
}

mxq::ODBC ETL::connect_to_source()
{
    mxq::ODBC source(config().src, config().timeout);

    if (!source.connect())
    {
        throw problem("Failed to connect to the source: ", source.error());
    }

    extractor().init_connection(source);
    extractor().start_thread(source, m_tables);
    return source;
}

std::pair<mxq::ODBC, mxq::ODBC> ETL::connect_to_both()
{
    mxq::ODBC source = connect_to_source();
    mxq::ODBC dest(config().dest, config().timeout);

    if (!dest.connect())
    {
        throw problem("Failed to connect to the destination: ", dest.error());
    }

    // Disabling UNIQUE_CHECKS, FOREIGN_KEY_CHECKS and AUTOCOMMIT will put InnoDB into a special mode
    // where inserting data is more efficient than it normally would be if the table is empty.
    auto SQL_SETUP = "SET MAX_STATEMENT_TIME=0, "
                     "SQL_MODE='ANSI_QUOTES,PIPES_AS_CONCAT,NO_ENGINE_SUBSTITUTION', "
                     "UNIQUE_CHECKS=0, FOREIGN_KEY_CHECKS=0, AUTOCOMMIT=0, SQL_NOTES=0";

    if (!dest.query(SQL_SETUP))
    {
        throw problem("Failed to setup connection: ", dest.error());
    }

    return {std::move(source), std::move(dest)};
}

void ETL::interrupt_source(mxq::ODBC& source)
{
    source.cancel();
}

void ETL::interrupt_both(std::pair<mxq::ODBC, mxq::ODBC>& connections)
{
    auto& [source, dest] = connections;
    source.cancel();
    dest.cancel();
}

Table* ETL::next_table()
{
    mxb_assert(!m_tables.empty());
    Table* rval = nullptr;

    if (size_t offset = m_counter.fetch_add(1, std::memory_order_relaxed); offset < m_tables.size())
    {
        rval = &m_tables[offset];
    }

    return rval;
}

bool ETL::checkpoint(int* current_checkpoint, Stage stage)
{
    std::unique_lock guard(m_lock);

    if (*current_checkpoint == m_next_checkpoint)
    {
        // This is the first thread to arrive at the latest checkpoint. Reset the table counter to start
        // iteration from the beginning.
        ++m_next_checkpoint;
        m_stage = stage;
        m_counter.store(0, std::memory_order_relaxed);
    }

    ++(*current_checkpoint);

    return !m_have_error;
}

void ETL::add_error()
{
    std::unique_lock guard(m_lock);
    m_have_error = true;
}

void ETL::cancel()
{
    std::lock_guard guard(m_lock);

    if (m_interruptor)
    {
        m_interruptor();
    }
}

template<auto connect_func, auto run_func, auto interrupt_func>
mxb::Json ETL::run_job()
{
    mxb::LogScope scope(m_id.c_str());
    std::string error;

    try
    {
        MXB_INFO("Starting ETL.");
        mxq::ODBC coordinator(m_config.src, m_config.timeout);

        if (!coordinator.connect())
        {
            throw problem(coordinator.error());
        }

        extractor().init_connection(coordinator);
        extractor().start(coordinator, m_tables);
        MXB_INFO("Coordinator connection created and initialized.");

        using Connection = decltype(std::invoke(connect_func, this));
        std::vector<Connection> connections;

        for (size_t i = 0; i < m_config.threads; i++)
        {
            connections.emplace_back(std::invoke(connect_func, this));
        }

        std::unique_lock guard(m_lock);
        m_interruptor = [&](){
            for (auto& c : connections)
            {
                std::invoke(interrupt_func, this, std::ref(c));
            }
        };
        guard.unlock();

        mxb_assert(connections.size() <= m_tables.size());

        MXB_INFO("Created %lu threads.", m_config.threads);
        extractor().threads_started(coordinator, m_tables);

        std::vector<std::thread> threads;

        for (auto& c : connections)
        {
            threads.emplace_back(run_func, this, std::ref(c));
        }

        std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
        MXB_INFO("ETL complete.");

        guard.lock();
        m_interruptor = nullptr;
        guard.unlock();
    }
    catch (const Error& e)
    {
        MXB_INFO("%s", e.what());
        error = e.what();
    }

    return to_json(error);
}

mxb::Json ETL::to_json(const std::string error)
{
    std::unique_lock guard(m_lock);
    mxb::Json rval(mxb::Json::Type::OBJECT);
    mxb::Json arr(mxb::Json::Type::ARRAY);
    bool ok = !m_have_error;

    for (const auto& t : m_tables)
    {
        arr.add_array_elem(t.to_json());
    }

    if (!error.empty())
    {
        ok = false;
        rval.set_string("error", error);
    }

    rval.set_bool("ok", ok);
    rval.set_string("stage", stage_to_str(m_stage));
    rval.set_object("tables", std::move(arr));

    return rval;
}

void ETL::run_prepare_job(mxq::ODBC& source) noexcept
{
    mxb::LogScope scope(m_id.c_str());

    while (auto t = next_table())
    {
        MXB_INFO("Read SQL: %s.%s", t->schema(), t->table());
        t->read_sql(source);
    }
}

void ETL::run_start_job(std::pair<mxq::ODBC, mxq::ODBC>& connections) noexcept
{
    mxb::LogScope scope(m_id.c_str());
    auto& [source, dest] = connections;
    int my_checkpoint = 0;

    while (auto t = next_table())
    {
        MXB_INFO("Read SQL: %s.%s", t->schema(), t->table());
        t->read_sql(source);
    }

    m_init_latch.arrive_and_wait();

    if (checkpoint(&my_checkpoint, Stage::PREPARE))
    {
        while (auto t = next_table())
        {
            MXB_INFO("Create objects: %s.%s", t->schema(), t->table());
            t->create_objects(source, dest);
        }

        m_create_latch.arrive_and_wait();

        if (checkpoint(&my_checkpoint, Stage::CREATE))
        {
            while (auto t = next_table())
            {
                MXB_INFO("Load data: %s.%s", t->schema(), t->table());
                t->load_data(source, dest);
            }

            m_load_latch.arrive_and_wait();
            checkpoint(&my_checkpoint, Stage::LOAD);
        }
    }
}

mxb::Json ETL::prepare()
{
    return run_job<&ETL::connect_to_source, &ETL::run_prepare_job, &ETL::interrupt_source>();
}

mxb::Json ETL::start()
{
    return run_job<&ETL::connect_to_both, &ETL::run_start_job, &ETL::interrupt_both>();
}
}
