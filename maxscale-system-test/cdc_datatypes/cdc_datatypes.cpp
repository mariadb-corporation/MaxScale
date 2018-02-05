/**
 * @file cdc_connect.cpp Test the CDC protocol
 */

#include "../testconnections.h"
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
    "DATETIME(1)",
    "DATETIME(2)",
    "DATETIME(3)",
    "DATETIME(4)",
    "DATETIME(5)",
    "DATETIME(6)",
    // TODO: Fix test setup to use same timezone
    // "TIMESTAMP",
    NULL
};

static const char* datetime_values[] =
{
    "'2018-01-01 11:11:11'",
    "NULL",
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
    "NULL",
    NULL
};

static const char* time_types[] =
{
    "TIME",
    "TIME(1)",
    "TIME(2)",
    "TIME(3)",
    "TIME(4)",
    "TIME(5)",
    "TIME(6)",
    NULL
};

static const char* time_values[] =
{
    "'12:00:00'",
    "NULL",
    NULL
};

struct
{
    const char** types;
    const char** values;
} test_set[]
{
    { integer_types,  integer_values  },
    { decimal_types,  decimal_values  },
    { string_types,   string_values   },
    { binary_types,   binary_values   },
    { datetime_types, datetime_values },
    { date_types,     date_values     },
    { time_types,     time_values     },
    { 0 }
};

void insert_data(TestConnections& test, const char *table, const char* type, const char** values)
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
        name = name.substr(0, offset);
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
    if (str[0] == '\"')
    {
        str = str.substr(1, str.length() - 2);
    }

    return str;
}

bool run_test(TestConnections& test)
{
    bool rval = true;

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
            CDC::Connection conn(test.maxscales->IP[0], 4001, "skysql", "skysql");

            if (conn.connect(name))
            {
                for (int j = 0; test_set[x].values[j]; j++)
                {
                    CDC::SRow row;

                    if ((row = conn.read()))
                    {
                        std::string input = unquote(test_set[x].values[j]);
                        std::string output = row->value(field_name);

                        if (input != output && (input != "NULL" || output != ""))
                        {
                            test.tprintf("Result mismatch: %s(%s) => %s",
                                         test_set[x].types[i], input.c_str(), output.c_str());
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

int main(int argc, char *argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections::check_nodes(false);
    TestConnections test(argc, argv);

    test.replicate_from_master(0);

    if (!run_test(test))
    {
        test.add_result(1, "Test failed");
    }

    test.check_maxscale_processes(0, 1);
    return test.global_result;
}
