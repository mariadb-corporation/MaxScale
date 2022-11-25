/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/sql_etl.hh"
#include "internal/servermanager.hh"

namespace
{
using Type = mxb::Json::Type;

template<class ... Args>
sql_etl::Error problem(Args&& ... args)
{
    std::ostringstream ss;
    (ss << ... << args);
    return sql_etl::Error(ss.str());
}

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

    return elem;
}
}

namespace sql_etl
{

std::unique_ptr<ETL> create(const mxb::Json& json,
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

    ss << "SERVER=" << dest_cc.host << ";"
       << "PORT=" << dest_cc.port << ";"
       << "UID=" << dest_cc.user << ";"
       << "PWD={" << dest_cc.password << "};";

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

    if (type_str == "mariadb")
    {
        // TODO: Create a MariaDBExtractor
    }
    else
    {
        throw problem("Unknown value for 'type': ", type_str);
    }

    std::string dest = ss.str();
    std::string src = src_cc.odbc_string;

    std::unique_ptr<ETL> etl = std::make_unique<ETL>(Config {src, dest}, std::move(extractor));

    for (const auto& val : get(json, "tables", Type::ARRAY).get_array_elems())
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

Table::Table(const ETL& etl,
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

mxb::Json Table::to_json() const
{
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

    return obj;
}

void Table::prepare() noexcept
{
    try
    {
        // TODO: implement this
    }
    catch (const Error& e)
    {
        m_error = e.what();
    }
}

void Table::start() noexcept
{
    try
    {
        // TODO: implement this
    }
    catch (const Error& e)
    {
        m_error = e.what();
    }
}

template<auto func>
mxb::Json ETL::run_job()
{
    std::vector<std::thread> threads;

    for (auto& t : m_tables)
    {
        threads.emplace_back(func, std::ref(t));
    }

    for (auto& thr : threads)
    {
        thr.join();
    }

    mxb::Json rval(mxb::Json::Type::OBJECT);
    mxb::Json arr(mxb::Json::Type::ARRAY);
    bool ok = true;

    for (const auto& t : m_tables)
    {
        arr.add_array_elem(t.to_json());

        if (!t.ok())
        {
            ok = false;
        }
    }

    rval.set_bool("ok", ok);
    rval.set_object("tables", std::move(arr));

    return rval;
}

mxb::Json ETL::prepare()
{
    return run_job<&Table::prepare>();
}

mxb::Json ETL::start()
{
    return run_job<&Table::start>();
}
}
