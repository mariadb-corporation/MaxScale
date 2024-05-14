/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "table.hh"

#include <maxbase/alloc.hh>
#include <maxbase/assert.hh>
#include <maxbase/string.hh>
#include <mysqld_error.h>

#include <string>

using namespace std::string_literals;

namespace
{

bool is_json_error(int errnum)
{
    switch (errnum)
    {
    case ER_JSON_BAD_CHR:
    case ER_JSON_NOT_JSON_CHR:
    case ER_JSON_EOS:
    case ER_JSON_SYNTAX:
    case ER_JSON_ESCAPING:
    case ER_JSON_DEPTH:
        return true;

    default:
        return false;
    }
}
}

namespace kafkaimporter
{

Table::Table(const std::string& table)
    : m_table(table)
{
}

Table::Table(Table&& rhs) noexcept
    : m_table(std::move(rhs.m_table))
    , m_stmt(rhs.m_stmt)
    , m_values(std::move(rhs.m_values))
    , m_lengths(std::move(rhs.m_lengths))
{
    rhs.m_stmt = nullptr;
}

Table& Table::operator=(Table&& rhs) noexcept
{
    if (&rhs != this)
    {
        m_table = std::move(rhs.m_table);
        m_values = std::move(rhs.m_values);
        m_lengths = std::move(rhs.m_lengths);
        mysql_stmt_close(m_stmt);
        m_stmt = rhs.m_stmt;
        rhs.m_stmt = nullptr;
    }

    return *this;
}

Table::~Table()
{
    free_values();
    mysql_stmt_close(m_stmt);
}

void Table::free_values()
{
    for (auto ptr : m_values)
    {
        MXB_FREE(ptr);
    }

    m_values.clear();
    m_lengths.clear();
}

bool Table::prepare(MYSQL* mysql, std::string engine)
{
    bool ok = false;
    mxb::upper_case(engine);

    // The table schema assumes the same data format that the MongoDB API in MaxScale uses. The "_id" field in
    // the JSON is expected to be populated. Currently the field is required as it has a unique index defined
    // for it. This can be changed with `ALTER TABLE ... DROP CONSTRAINT id_is_not_null`.
    std::string create = "CREATE TABLE IF NOT EXISTS " + m_table;

    if (engine == "INNODB")
    {
        create += "("
                  "data JSON NOT NULL, "
                  "id VARCHAR(1024) AS (JSON_EXTRACT(data, '$._id')) UNIQUE KEY, "
                  "CONSTRAINT id_is_not_null CHECK(JSON_EXTRACT(data, '$._id') IS NOT NULL) "
                  ") ENGINE=INNODB";
    }
    else
    {
        create += "(data JSON NOT NULL) ENGINE=" + std::move(engine);
    }

    if (mysql_query(mysql, create.c_str()) == 0)
    {
        std::string query = "INSERT INTO " + m_table + "(data) VALUES (?)";
        m_stmt = mysql_stmt_init(mysql);

        if (mysql_stmt_prepare(m_stmt, query.c_str(), query.size()) == 0)
        {
            ok = true;
        }
        else
        {
            MXB_ERROR("Failed to prepare statement: %s", mysql_stmt_error(m_stmt));
        }
    }
    else
    {
        MXB_ERROR("Failed to create table `%s`: %s", m_table.c_str(), mysql_error(mysql));
    }

    return ok;
}

bool Table::insert(const std::string& value)
{
    m_values.push_back(MXB_STRDUP(value.c_str()));
    m_lengths.push_back(value.length());
    return true;
}

bool Table::flush()
{
    mxb_assert(m_stmt);
    bool ok = true;

    if (!m_values.empty())
    {
        unsigned int array_size = m_values.size();
        mysql_stmt_attr_set(m_stmt, STMT_ATTR_ARRAY_SIZE, &array_size);

        MYSQL_BIND bind = {};
        bind.buffer = m_values.data();
        bind.length = m_lengths.data();
        bind.buffer_type = MYSQL_TYPE_STRING;
        mysql_stmt_bind_param(m_stmt, &bind);

        if (mysql_stmt_execute(m_stmt) != 0)
        {
            auto errnum = mysql_stmt_errno(m_stmt);
            auto error = mysql_stmt_error(m_stmt);

            if (is_json_error(errnum))
            {
                MXB_INFO("Ignoring malformed JSON: %d, %s", errnum, error);
            }
            else if (errnum == ER_DUP_ENTRY)
            {
                MXB_INFO("Ignoring record with duplicate value for key `_id`: %d, %s", errnum, error);
            }
            else if (errnum == ER_CONSTRAINT_FAILED)
            {
                MXB_INFO("Ignoring record due to constraint failure: %d, %s", errnum, error);
            }
            else
            {
                MXB_ERROR("Failed to insert value into '%s': %d, %s", m_table.c_str(), errnum, error);
                ok = false;
            }
        }

        free_values();
    }

    return ok;
}
}
