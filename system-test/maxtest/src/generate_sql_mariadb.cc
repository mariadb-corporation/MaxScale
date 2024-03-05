/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
std::array integer_types
{
    "TINYINT",
    "SMALLINT",
    "MEDIUMINT",
    "INT",
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
    "FLOAT",
    "DOUBLE",
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
    "CHAR(50)",
    "VARCHAR(50)",
    "TINYTEXT",
    "TEXT",
    "MEDIUMTEXT",
    "LONGTEXT",
};

std::array string_values
{
    "\"Hello world!\"",
    "\"The quick brown fox jumps over the lazy dog\"",
    "NULL",
};

std::array binary_types
{
    "BINARY(50)",
    "VARBINARY(50)",
    "TINYBLOB",
    "BLOB",
    "MEDIUMBLOB",
    "LONGBLOB",
};

std::array binary_values
{
    "\"Hello world!\"",
    "\"The quick brown fox jumps over the lazy dog\"",
    "NULL",
};

std::array datetime_types
{
    "DATETIME",
};

std::array datetime_values
{
    "'2018-01-01 11:11:11'",
    "'0-00-00 00:00:00'",
    "NULL",
};

std::array datetime2_types
{
    "DATETIME(6)",
};

std::array datetime2_values
{
    "'2018-01-01 11:11:11.000001'",
    "'2018-01-01 11:11:11.000010'",
    "'2018-01-01 11:11:11.000100'",
    "'2018-01-01 11:11:11.001000'",
    "'2018-01-01 11:11:11.010000'",
    "'2018-01-01 11:11:11.100000'",
    "'0-00-00 00:00:00.000000'",
    "NULL",
};

std::array timestamp_types
{
    "TIMESTAMP",
};

std::array timestamp_values
{
    "'2018-01-01 11:11:11'",
    "'0-00-00 00:00:00'",
};

std::array timestamp2_types
{
    "TIMESTAMP(6)",
};

std::array timestamp2_values
{
    "'2018-01-01 11:11:11.000001'",
    "'2018-01-01 11:11:11.000010'",
    "'2018-01-01 11:11:11.000100'",
    "'2018-01-01 11:11:11.001000'",
    "'2018-01-01 11:11:11.010000'",
    "'2018-01-01 11:11:11.100000'",
    "'0-00-00 00:00:00.000000'",
};

std::array date_types
{
    "DATE",
};

std::array date_values
{
    "'2018-01-01'",
    "'0-00-00'",
    "NULL",
};

std::array time_types
{
    "TIME",
};

std::array time_values
{
    "'12:00:00'",
    "NULL",
};

std::array time2_types
{
    "TIME(6)",
};

std::array time2_values
{
    "'12:00:00.000001'",
    "'12:00:00.000010'",
    "'12:00:00.000100'",
    "'12:00:00.001000'",
    "'12:00:00.010000'",
    "'12:00:00.100000'",
    "NULL",
};

std::array serial_types
{
    "SERIAL",   // This is just an alias for a BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY
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
    "INET6",
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

std::vector<sql_generation::SQLType> init()
{
    std::vector<sql_generation::SQLType> rval;

    sql_generation::impl::add_test(integer_types, integer_values, rval);
    sql_generation::impl::add_test(decimal_types, decimal_values, rval);
    sql_generation::impl::add_test(string_types, string_values, rval);
    sql_generation::impl::add_test(binary_types, binary_values, rval);
    sql_generation::impl::add_test(datetime_types, datetime_values, rval);
    sql_generation::impl::add_test(timestamp_types, timestamp_values, rval);
    sql_generation::impl::add_test(date_types, date_values, rval);
    sql_generation::impl::add_test(time_types, time_values, rval);
    sql_generation::impl::add_test(datetime2_types, datetime2_values, rval);
    sql_generation::impl::add_test(timestamp2_types, timestamp2_values, rval);
    sql_generation::impl::add_test(serial_types, serial_values, rval);
    sql_generation::impl::add_test(json_types, json_values, rval);
    sql_generation::impl::add_test(inet6_types, inet6_values, rval);

    return rval;
}
}

namespace sql_generation
{
const std::vector<SQLType>& mariadb_types()
{
    static auto test_set = init();
    return test_set;
}
}
