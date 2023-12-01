/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxtest/testconnections.hh>
#include <maxtest/maxrest.hh>

using namespace std;

namespace
{

string get_parameter_value(MaxRest& rest, const char* zParameter)
{
    auto json = rest.v1_services("RW-Split-Router");
    auto parameters = json.at("/data/attributes/parameters");

    return parameters.get_string(zParameter);
}

string get_server_variable(Connection& c, const char* zVariable)
{
    string query = "SELECT @@global.";
    query += zVariable;

    Row row = c.row(query);

    return row[0];
}

bool set_server_variable(Connection& c,  const char* zVariable, const string& value)
{
    string query = "SET @@global.";
    query += zVariable;
    query += " = ";
    query += value;

    return c.query(query);
}

}

string touch_connection_keepalive(const string& value)
{
    // We know connection keepalive is 80% of the minimum wait_timeout
    // value of all servers used by the service and we assume that it
    // initially is the same everywhere. Thus, by reducing the value
    // of wait_timeout on one, it should affect connection_keepalive
    // of the service.
    long l = std::stol(value);
    l *= 0.8;

    return std::to_string(l);
}

struct AutoTuneCase
{
    const char* zMaxScale_parameter;
    const char* zServer_variable;
    string (*touch)(const string& value);
};

AutoTuneCase auto_tune_cases[] =
{
    {
        "connection_keepalive",
        "wait_timeout",
        touch_connection_keepalive
    }
};

void check(TestConnections& test, MaxRest& rest, Connection& c, const AutoTuneCase& auto_tune_case)
{
    string parameter_was = get_parameter_value(rest, auto_tune_case.zMaxScale_parameter);
    string variable_was = get_server_variable(c, auto_tune_case.zServer_variable);

    cout << "Variable: " << variable_was << ", parameter: " << parameter_was << endl;

    string variable_is = auto_tune_case.touch(variable_was);

    test.expect(set_server_variable(c, auto_tune_case.zServer_variable, variable_is),
                "Could not update variable: %s", c.error());

    // Currently the variable values are fetched by the Monitor every 10 seconds.
    sleep(10);
    test.maxscale->wait_for_monitor(2); // To make sure auto_tune has picked up the current values.

    string parameter_is = get_parameter_value(rest, auto_tune_case.zMaxScale_parameter);
    cout << "Variable: " << variable_is << ", parameter: " << parameter_is << endl;

    test.expect(parameter_is != parameter_was, "Parameter value is still the same.");

    // Restore the situation.
    test.expect(set_server_variable(c, auto_tune_case.zServer_variable, variable_was),
                "Could not reset variable: %s", c.error());
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.maxscale->wait_for_monitor(2); // To make sure auto_tune has picked up the current values.

    MaxRest rest(&test);
    Connection c = test.repl->get_connection(0);

    test.expect(c.connect(), "Could not connect to MariaDB node: %s", c.error());

    for (const auto& auto_tune_case : auto_tune_cases)
    {
        check(test, rest, c, auto_tune_case);
    }

    return test.global_result;
}
