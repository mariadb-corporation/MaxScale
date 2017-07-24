/**
 * @file fwf - Firewall filtyer test
 * - setup Firewall filter to use rules from rule file fw/ruleXX, where XX - number of sub-test
 * - execute queries for fw/passXX file, expect OK
 * - execute queries from fw/denyXX, expect Access Denied error (mysql_error 1141)
 * - repeat for all XX
 * - setup Firewall filter to block queries next 2 minutes using 'at_time' statement (see template fw/rules_at_time)
 * - start sending queries, expect Access Denied now and OK after two mintes
 * - setup Firewall filter to limit a number of queries during certain time
 * - start sending queries as fast as possible, expect OK for N first quries and Access Denied for next queries
 * - wait, start sending queries again, but only one query per second, expect OK
 * - try to load rules with syntax error, expect failure for all sessions and queries
 */


#include <iostream>
#include <ctime>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "fw_copy_rules.h"

int read_and_execute_queries(TestConnections *Test, const char* filename, int expected)
{
    FILE *file = fopen(filename, "r");
    int local_result = 0;
    if (file != NULL)
    {
        char sql[4096];
        while (fgets(sql, sizeof(sql), file))
        {
            Test->set_timeout(60);
            if (strlen(sql) > 1)
            {
                Test->tprintf("%s", sql);
                if (execute_query(Test->conn_rwsplit, sql) != expected &&
                        (expected == 1 || mysql_errno(Test->conn_rwsplit) == 1141))
                {
                    Test->tprintf("Query %s, but %s expected, MySQL error: %d, %s\n",
                                  expected ? "succeeded" : "failed",
                                  expected ? "failure" : "success",
                                  mysql_errno(Test->conn_rwsplit), mysql_error(Test->conn_rwsplit));
                    local_result++;
                }
            }
        }
        fclose(file);
    }
    else
    {
        Test->add_result(1, "Error opening file '%s'\n", filename);
    }
    return local_result;
}

int main(int argc, char *argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections * Test = new TestConnections(argc, argv);
    int local_result;
    char str[4096];
    char pass_file[4096];
    char deny_file[4096];
    char rules_dir[4096];
    FILE* file;

    sprintf(rules_dir, "%s/fw2/", test_dir);
    int N = 5;
    int i;

    for (i = 1; i < N + 1; i++)
    {
        Test->set_timeout(60);
        local_result = 0;

        Test->stop_maxscale();

        sprintf(str, "rules%d", i);
        copy_rules(Test, str, rules_dir);

        Test->start_maxscale();
        Test->connect_rwsplit();

        sprintf(pass_file, "%s/fw2/pass%d", test_dir, i);
        sprintf(deny_file, "%s/fw2/deny%d", test_dir, i);

        Test->tprintf("********** Trying queries that should be OK ********** \n");
        local_result += read_and_execute_queries(Test, pass_file, 0);

        Test->tprintf("********** Trying queries that should FAIL ********** \n");
        local_result += read_and_execute_queries(Test, deny_file, 1);

        Test->add_result(local_result, "********** rules%d test FAILED\n", i);
        mysql_close(Test->conn_rwsplit);
    }

    Test->check_maxscale_processes(1);

    int rval = Test->global_result;
    delete Test;
    return rval;
}


