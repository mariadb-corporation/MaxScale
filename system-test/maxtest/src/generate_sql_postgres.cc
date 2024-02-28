/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/generate_sql.hh>
#include <maxbase/string.hh>

#include <tuple>
#include <array>

namespace
{

std::array boolean_types
{
    "BOOLEAN",
};

std::array boolean_values
{
    "TRUE",
    "FALSE",
};

std::array integer_types
{
    "SMALLINT",
    "INTEGER",
    "BIGINT",
};

std::array integer_values
{
    "0",
    "1",
    "-1",
    "20",
    "-20",
    "NULL",
};

std::array decimal_types
{
    "REAL",
    "DOUBLE PRECISION",
    "DECIMAL(10, 2)",
    "DECIMAL(32, 2)",
};

std::array decimal_values
{
    "0",
    "1.5",
    "-1.5",
    "20.5",
    "-20.5",
    "NULL",
};

std::array string_types
{
    "TEXT",
};

std::array string_values
{
    "'Hello world!'",
    "'The quick brown fox jumps over the lazy dog'",
    "NULL",
};

std::array binary_types
{
    "BYTEA",
};

std::array binary_values
{
    "'Hello world!'",
    "'The quick brown fox jumps over the lazy dog'",
    "NULL",
};

std::array timestamp_types
{
    "TIMESTAMP",
};

std::array timestamp_values
{
    "'2018-01-01 11:11:11'",
    "'2018-01-01 11:11:11.000001'",
    "'2018-01-01 11:11:11.000010'",
    "'2018-01-01 11:11:11.000100'",
    "'2018-01-01 11:11:11.001000'",
    "'2018-01-01 11:11:11.010000'",
    "'2018-01-01 11:11:11.100000'",
    "NULL",
};

std::array time_types
{
    "TIME",
};

std::array time_values
{
    "'12:00:00'",
    "'00:00:00'",
    "'23:59:59'",
    "NULL",
};

std::array serial_types
{
    "SMALLSERIAL PRIMARY KEY",
    "SERIAL PRIMARY KEY",
    "BIGSERIAL PRIMARY KEY",
};

std::array serial_values
{
    "1",
    "2",
    "3",
    "4",
};

std::array json_types
{
    "JSON",
    "JSONB",
};

std::array json_values
{
    "'{\"hello\": \"world\"}'",
    "'{\"one\": 1}'",
    "'{\"array\": []}'",
    "NULL",
};

std::array inet6_types
{
    "INET",
};

std::array inet6_values
{
    "'2001:db8::ff00:42:8329'",
    "'2001:0db8:0000:0000:0000:ff00:0042:8329'",
    "'::ffff:192.0.2.128'",
    "'::'",
    "'::1'",
    "NULL",
};

std::array uuid_types
{
    "UUID",
};

std::array uuid_values
{
    "'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'",
    "NULL",
};

std::array xml_types
{
    "XML",
};

std::array xml_values
{
    "'<hello><world>Some value</world></hello>'",
    "NULL",
};

static std::vector<sql_generation::SQLType> init()
{
    std::vector<sql_generation::SQLType> rval;
    const char* q = "\"";
    sql_generation::impl::add_test(integer_types, integer_values, rval, q);
    sql_generation::impl::add_test(decimal_types, decimal_values, rval, q);
    sql_generation::impl::add_test(string_types, string_values, rval, q);
    sql_generation::impl::add_test(binary_types, binary_values, rval, q);
    sql_generation::impl::add_test(timestamp_types, timestamp_values, rval, q);
    sql_generation::impl::add_test(time_types, time_values, rval, q);
    sql_generation::impl::add_test(serial_types, serial_values, rval, q);
    sql_generation::impl::add_test(json_types, json_values, rval, q);
    sql_generation::impl::add_test(inet6_types, inet6_values, rval, q);
    sql_generation::impl::add_test(boolean_types, boolean_values, rval, q);
    sql_generation::impl::add_test(uuid_types, uuid_values, rval, q);
    sql_generation::impl::add_test(xml_types, xml_values, rval, q);

    return rval;
}
}

namespace sql_generation
{
const std::vector<SQLType>& postgres_types()
{
    static auto test_set = init();
    return test_set;
}
}
