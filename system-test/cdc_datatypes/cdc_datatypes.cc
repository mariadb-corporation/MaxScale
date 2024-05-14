/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * @file cdc_connect.cpp Test the CDC protocol
 */

#include <maxtest/testconnections.hh>
#include <maxtest/generate_sql.hh>
#include <maxtest/cdc_connector.h>
#include "cdc_result.h"
#include <iostream>
#include <stdio.h>

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
    test.maxscale->start();

    std::set<std::string> excluded = {"JSON", "INET6"};
    std::vector<sql_generation::SQLType> test_set;

    for (const auto& t : sql_generation::mariadb_types())
    {
        if (excluded.count(t.type_name) == 0)
        {
            test_set.push_back(t);
        }
    }

    test.log_printf("Inserting data");
    for (const auto& t : test_set)
    {
        test.repl->connect();
        execute_query(test.repl->nodes[0], "%s", t.create_sql.c_str());

        for (const auto& v : t.values)
        {
            execute_query(test.repl->nodes[0], "%s", v.insert_sql.c_str());
        }

        execute_query(test.repl->nodes[0], "%s", t.drop_sql.c_str());
        test.repl->close_connections();
    }

    test.log_printf("Waiting for avrorouter to process data");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "FLUSH LOGS");
    test.repl->close_connections();
    sleep(10);

    for (const auto& t : test_set)
    {
        test.reset_timeout();
        test.log_printf("Testing type: %s", t.type_name.c_str());
        CDC::Connection conn(test.maxscale->ip4(), 4001, "skysql", "skysql");

        if (conn.connect(t.full_name))
        {
            for (const auto& v : t.values)
            {
                CDC::SRow row;

                if ((row = conn.read()))
                {
                    std::string input = unquote(v.value);
                    std::string output = row->value(t.field_name);
                    std::string unhex_prefix = "UNHEX('";
                    std::string unhex_suffix = "')";

                    if (input.substr(0, unhex_prefix.size()) == unhex_prefix)
                    {
                        input = input.substr(unhex_prefix.size(),
                                             input.size() - unhex_prefix.size() - unhex_suffix.size());
                    }

                    if (input == output || (input == "NULL" && (output == "" || output == "0")))
                    {
                        // Expected result
                    }
                    else
                    {
                        test.log_printf("Result mismatch: %s(%s) => %s",
                                        t.type_name.c_str(),
                                        input.c_str(),
                                        output.c_str());
                        rval = false;
                    }
                }
                else
                {
                    std::string err = conn.error();
                    test.log_printf("Failed to read data: %s", err.c_str());
                }
            }
        }
        else
        {
            std::string err = conn.error();
            test.log_printf("Failed to request data: %s", err.c_str());
            rval = false;
            break;
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

    test.maxscale->expect_running_status(true);

    return test.global_result;
}
