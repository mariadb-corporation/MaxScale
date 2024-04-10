/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/string.hh>

#include <vector>
#include <string>

namespace sql_generation
{
struct SQLTypeValue
{
    // The SQL for inserting the data
    std::string insert_sql;

    // The plain SQL value
    std::string value;
};

struct SQLType
{
    // The name of the SQL type
    std::string type_name;

    // The name of the field in the table
    std::string field_name;

    // The name of the table
    std::string table_name;

    // The name of the database the table is in
    std::string database_name;

    // The fully qualified name of the table
    std::string full_name;

    // SQL for creating the table
    std::string create_sql;

    // SQL for dropping the table
    std::string drop_sql;

    std::vector<SQLTypeValue> values;
};

/**
 * Get SQL for creating all MariaDB data types
 *
 * @return The MariaDB types
 */
const std::vector<SQLType>& mariadb_types();

/**
 * Get SQL for creating most PostgreSQL data types
 *
 * Things like OIDs etc. are not included.
 *
 * @return The PostgreSQL types
 */
const std::vector<SQLType>& postgres_types();

// Internal implementation namespace
namespace impl
{
static inline std::string type_to_table_name(std::string_view type)
{
    std::string name = mxb::cat("type_", type);
    size_t offset = name.find('(');

    if (offset != std::string::npos)
    {
        name[offset] = '_';

        offset = name.find(')');

        if (offset != std::string::npos)
        {
            name = name.substr(0, offset);
        }

        offset = name.find(',');

        if (offset != std::string::npos)
        {
            name = name.substr(0, offset);
        }
    }

    offset = name.find(' ');

    if (offset != std::string::npos)
    {
        name = name.substr(0, offset);
    }

    return name;
}

template<class Types, class Values>
void add_test(Types&& types,
              Values&& values,
              std::vector<sql_generation::SQLType>& output,
              const char* quote = "")
{
    constexpr const std::string_view DATABASE_NAME = "test";
    constexpr const std::string_view FIELD_NAME = "a";

    for (const auto& type : types)
    {
        sql_generation::SQLType sql_type;
        std::string name = type_to_table_name(type);
        sql_type.field_name = FIELD_NAME;
        sql_type.database_name = DATABASE_NAME;
        sql_type.table_name = name;
        sql_type.full_name = mxb::cat(quote, DATABASE_NAME, quote, ".", quote, name, quote);
        sql_type.type_name = type;

        sql_type.create_sql = mxb::cat("CREATE TABLE ", sql_type.full_name,
                                       " (", FIELD_NAME, " ", type, ")");
        sql_type.drop_sql = mxb::cat("DROP TABLE ", sql_type.full_name);

        for (const auto& value : values)
        {
            sql_generation::SQLTypeValue sql_value;
            sql_value.value = value;
            sql_value.insert_sql = mxb::cat("INSERT INTO ", sql_type.full_name, " VALUES (", value, ")");
            sql_type.values.push_back(std::move(sql_value));
        }

        output.push_back(std::move(sql_type));
    }
}
}
}
