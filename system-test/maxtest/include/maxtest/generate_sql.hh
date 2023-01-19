/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>

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
}
