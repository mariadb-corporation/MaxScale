/**
 * @file cdc_connect.cpp Test the CDC protocol
 */

#include "../testconnections.h"
#include "../cdc_connector.h"
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
//    "\"The Unicode should work: äöåǢ\"",
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
//    "\"The Unicode should work: äöåǢ\"",
//    "\"These should work for binary types: ⦿☏☃☢😤😂\"",
    NULL
};

static const char* datetime_types[] =
{
    "DATETIME(1)",
    "DATETIME(2)",
    "DATETIME(3)",
    "DATETIME(4)",
    "DATETIME(5)",
    "DATETIME(6)",
    "TIMESTAMP",
    NULL
};

static const char* datetime_values[] =
{
    "'2018-01-01 11:11:11'",
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
            CDC::Connection conn(test.maxscale_IP, 4001, "skysql", "skysql");

            if (conn.createConnection() && conn.requestData(name))
            {
                for (int j = 0; test_set[x].values[j]; j++)
                {
                    std::string row;

                    if (conn.readRow(row))
                    {
                        TestInput input(test_set[x].values[j], test_set[x].types[i]);
                        TestOutput output(row, field_name);

                        if (input != output)
                        {
                            test.tprintf("Result mismatch: %s(%s) => %s",
                                         test_set[x].types[i], test_set[x].values[j], output.getValue().c_str());
                            rval = false;
                        }
                    }
                    else
                    {
                        std::string err = conn.getError();
                        test.tprintf("Failed to read data: %s", err.c_str());
                    }
                }
            }
            else
            {
                std::string err = conn.getError();
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

    test.replicate_from_master();

    if (!run_test(test))
    {
        test.add_result(1, "Test failed");
    }

    test.check_maxscale_processes(1);
    return test.global_result;
}
