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

namespace sql_etl
{

std::unique_ptr<Config> configure(const mxb::Json& json, const HttpSql::ConnectionConfig& cc)
{
    auto cnf = std::make_unique<Config>();

    // TODO:: Implement

    return cnf;
}

std::unique_ptr<ETL> create(const Config& config, const mxb::Json& json)
{
    std::unique_ptr<ETL> etl = std::make_unique<ETL>(config);

    // TODO:: Implement

    return etl;
}

Table::Table(const ETL& etl,
             std::string_view schema,
             std::string_view table,
             std::string_view create,
             std::string_view select)
    : m_etl(etl)
    , m_schema(schema)
    , m_table(table)
    , m_create(create)
    , m_select(select)
{
}

void Table::read_create() noexcept
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

void Table::read_select() noexcept
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
    rval.set_bool("ok", true);
    return rval;
}

mxb::Json ETL::list_tables()
{
    mxb::Json rval(mxb::Json::Type::OBJECT);
    rval.set_bool("ok", true);
    return rval;
}

mxb::Json ETL::get_create()
{
    return run_job<&Table::read_create>();
}

mxb::Json ETL::get_select()
{
    return run_job<&Table::read_select>();
}

mxb::Json ETL::start()
{
    return run_job<&Table::start>();
}
}
