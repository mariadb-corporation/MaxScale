/**
 * @file cdc_connect.cpp Test the CDC protocol
 */

#include <maxtest/testconnections.hh>
#include <cdc_connector.h>
#include "cdc_result.h"
#include <iostream>
#include <stdio.h>

static const char* table_name = "test.type";
static const char* field_name = "a";

static const char* integer_types[] =
{
    "TINYINT",
    "SMALLINT",
    "MEDIUMINT",
    "INT",
    "BIGINT",
    NULL
};

static const char* integer_values[] =
{
    "0",
    "1",
    "-1",
    "20",
    "-20",
    "NULL",
    NULL
};

static const char* decimal_types[] =
{
    "FLOAT",
    "DOUBLE",
    "DECIMAL(10, 2)",
    "DECIMAL(32, 2)",
    NULL
};

static const char* decimal_values[] =
{
    "0",
    "1.5",
    "-1.5",
    "20.5",
    "-20.5",
    "NULL",
    NULL
};

static const char* string_types[] =
{
    "CHAR(50)",
    "VARCHAR(50)",
    "TINYTEXT",
    "TEXT",
    "MEDIUMTEXT",
    "LONGTEXT",
    NULL
};

static const char* string_values[] =
{
    "\"Hello world!\"",
    "\"The quick brown fox jumps over the lazy dog\"",
    "NULL",
    NULL
};

static const char* binary_types[] =
{
    "BINARY(50)",
    "VARBINARY(50)",
    "TINYBLOB",
    "BLOB",
    "MEDIUMBLOB",
    "LONGBLOB",
    NULL
};

static const char* binary_values[] =
{
    "\"Hello world!\"",
    "\"The quick brown fox jumps over the lazy dog\"",
    "NULL",
    NULL
};

static const char* datetime_types[] =
{
    "DATETIME",
    NULL
};

static const char* datetime_values[] =
{
    "'2018-01-01 11:11:11'",
    "'0-00-00 00:00:00'",
    "NULL",
    NULL
};

static const char* datetime2_types[] =
{
    "DATETIME(6)",
    NULL
};

static const char* datetime2_values[] =
{
    "'2018-01-01 11:11:11.000001'",
    "'2018-01-01 11:11:11.000010'",
    "'2018-01-01 11:11:11.000100'",
    "'2018-01-01 11:11:11.001000'",
    "'2018-01-01 11:11:11.010000'",
    "'2018-01-01 11:11:11.100000'",
    "'0-00-00 00:00:00.000000'",
    "NULL",
    NULL
};

static const char* timestamp_types[] =
{
    "TIMESTAMP",
    NULL
};

static const char* timestamp_values[] =
{
    "'2018-01-01 11:11:11'",
    "'0-00-00 00:00:00'",
    NULL
};

static const char* timestamp2_types[] =
{
    "TIMESTAMP(6)",
    NULL
};

static const char* timestamp2_values[] =
{
    "'2018-01-01 11:11:11.000001'",
    "'2018-01-01 11:11:11.000010'",
    "'2018-01-01 11:11:11.000100'",
    "'2018-01-01 11:11:11.001000'",
    "'2018-01-01 11:11:11.010000'",
    "'2018-01-01 11:11:11.100000'",
    "'0-00-00 00:00:00.000000'",
    NULL
};

static const char* date_types[] =
{
    "DATE",
    NULL
};

static const char* date_values[] =
{
    "'2018-01-01'",
    "'0-00-00'",
    "NULL",
    NULL
};

static const char* time_types[] =
{
    "TIME",
    NULL
};

static const char* time_values[] =
{
    "'12:00:00'",
    "NULL",
    NULL
};

static const char* time2_types[] =
{
    "TIME(6)",
    NULL
};

static const char* time2_values[] =
{
    "'12:00:00.000001'",
    "'12:00:00.000010'",
    "'12:00:00.000100'",
    "'12:00:00.001000'",
    "'12:00:00.010000'",
    "'12:00:00.100000'",
    "NULL",
    NULL
};

struct
{
    const char** types;
    const char** values;
} test_set[]
{
    {integer_types, integer_values},
    {decimal_types, decimal_values},
    {string_types, string_values},
    {binary_types, binary_values},
    {datetime_types, datetime_values},
    {timestamp_types, timestamp_values},
    {date_types, date_values},
    {time_types, time_values},
    {datetime2_types, datetime2_values},
    {timestamp2_types, timestamp2_values},
    {time2_types, time2_values},
    {0, 0}
};

void insert_data(TestConnections& test, const char* table, const char* type, const char** values)
{
    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE TABLE %s(%s %s)", table, field_name, type);

    for (int i = 0; values[i]; i++)
    {
        execute_query(test.repl->nodes[0], "INSERT INTO %s VALUES (%s)", table, values[i]);
    }

    execute_query(test.repl->nodes[0], "DROP TABLE %s", table);
    test.repl->close_connections();
}

std::string type_to_table_name(const char* type)
{
    std::string name = table_name;
    name += "_";
    name += type;

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

static std::string unquote(std::string str)
{
    if (str[0] == '\"' || str[0] == '\'')
    {
        str = str.substr(1, str.length() - 2);
    }

    return str;
}

bool run_test(TestConnections& test)
{
    bool rval = true;

    test.repl->connect();
    execute_query(test.repl->nodes[0], "RESET MASTER");
    test.repl->close_connections();
    test.maxscales->start();

    test.tprintf("Inserting data");
    for (int x = 0; test_set[x].types; x++)
    {
        for (int i = 0; test_set[x].types[i]; i++)
        {
            std::string name = type_to_table_name(test_set[x].types[i]);
            insert_data(test, name.c_str(), test_set[x].types[i], test_set[x].values);
        }
    }

    test.tprintf("Waiting for avrorouter to process data");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "FLUSH LOGS");
    test.repl->close_connections();
    sleep(10);

    for (int x = 0; test_set[x].types; x++)
    {
        for (int i = 0; test_set[x].types[i]; i++)
        {
            test.set_timeout(60);
            test.tprintf("Testing type: %s", test_set[x].types[i]);
            std::string name = type_to_table_name(test_set[x].types[i]);
            CDC::Connection conn(test.maxscales->ip4(0), 4001, "skysql", "skysql");

            if (conn.connect(name))
            {
                for (int j = 0; test_set[x].values[j]; j++)
                {
                    CDC::SRow row;

                    if ((row = conn.read()))
                    {
                        std::string input = unquote(test_set[x].values[j]);
                        std::string output = row->value(field_name);

                        if (input == output || (input == "NULL" && (output == "" || output == "0")))
                        {
                            // Expected result
                        }
                        else
                        {
                            test.tprintf("Result mismatch: %s(%s) => %s",
                                         test_set[x].types[i],
                                         input.c_str(),
                                         output.c_str());
                            rval = false;
                        }
                    }
                    else
                    {
                        std::string err = conn.error();
                        test.tprintf("Failed to read data: %s", err.c_str());
                    }
                }
            }
            else
            {
                std::string err = conn.error();
                test.tprintf("Failed to request data: %s", err.c_str());
                rval = false;
                break;
            }
            test.stop_timeout();
        }
    }
    return rval;
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    if (!run_test(test))
    {
        test.add_result(1, "Test failed");
    }

    test.maxscales->expect_running_status(true);

    return test.global_result;
}
