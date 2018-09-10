/**
 * @file fwf - Firewall filter test (also regression test for MXS-683 "qc_mysqlembedded reports as-name
 * instead of original-name")
 * - setup Firewall filter to use rules from rule file fw/ruleXX, where XX - number of sub-test
 * - execute queries for fw/passXX file, expect OK
 * - execute queries from fw/denyXX, expect Access Denied error (mysql_error 1141)
 * - repeat for all XX
 * - setup Firewall filter to block queries next 2 minutes using 'at_time' statement (see template
 * fw/rules_at_time)
 * - start sending queries, expect Access Denied now and OK after two mintes
 * - setup Firewall filter to limit a number of queries during certain time
 * - start sending queries as fast as possible, expect OK for N first quries and Access Denied for next
 * queries
 * - wait, start sending queries again, but only one query per second, expect OK
 * - try to load rules with syntax error, expect failure for all sessions and queries
 */


#include <iostream>
#include <ctime>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "fw_copy_rules.h"

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections* Test = new TestConnections(argc, argv);
    int local_result;
    char str[4096];
    char sql[4096];
    char pass_file[4096];
    char deny_file[4096];
    char rules_dir[4096];
    FILE* file;

    Test->maxscales->ssh_node_f(0,
                                true,
                                "cd %s;"
                                "rm -rf rules;"
                                "mkdir rules;"
                                "chown vagrant:vagrant rules",
                                Test->maxscales->access_homedir[0]);

    sprintf(rules_dir, "%s/fw/", test_dir);
    int N = 18;
    int i;

    for (i = 1; i < N + 1; i++)
    {
        Test->set_timeout(180);
        local_result = 0;

        sprintf(str, "rules%d", i);
        copy_rules(Test, str, rules_dir);

        Test->maxscales->restart_maxscale(0);
        Test->maxscales->connect_rwsplit(0);

        sprintf(pass_file, "%s/fw/pass%d", test_dir, i);
        sprintf(deny_file, "%s/fw/deny%d", test_dir, i);

        if (Test->verbose)
        {
            Test->tprintf("Pass file: %s", pass_file);
            Test->tprintf("Deny file: %s", deny_file);
        }

        file = fopen(pass_file, "r");
        if (file != NULL)
        {
            if (Test->verbose)
            {
                Test->tprintf("********** Trying queries that should be OK ********** ");
            }
            while (fgets(sql, sizeof(sql), file))
            {
                if (strlen(sql) > 1)
                {
                    if (Test->verbose)
                    {
                        Test->tprintf("%s", sql);
                    }
                    int rv = execute_query(Test->maxscales->conn_rwsplit[0], "%s", sql);
                    Test->add_result(rv, "Query should succeed: %s", sql);
                    local_result += rv;
                }
            }
            fclose(file);
        }
        else
        {
            Test->add_result(1, "Error opening query file");
        }

        file = fopen(deny_file, "r");
        if (file != NULL)
        {
            if (Test->verbose)
            {
                Test->tprintf("********** Trying queries that should FAIL ********** ");
            }
            while (fgets(sql, sizeof(sql), file))
            {
                Test->set_timeout(180);
                if (strlen(sql) > 1)
                {
                    if (Test->verbose)
                    {
                        Test->tprintf("%s", sql);
                    }
                    execute_query_silent(Test->maxscales->conn_rwsplit[0], sql);
                    if (mysql_errno(Test->maxscales->conn_rwsplit[0]) != 1141)
                    {
                        Test->tprintf("Expected 1141, Access Denied but got %d, %s instead: %s",
                                      mysql_errno(Test->maxscales->conn_rwsplit[0]),
                                      mysql_error(Test->maxscales->conn_rwsplit[0]),
                                      sql);
                        local_result++;
                    }
                }
            }
            fclose(file);
        }
        else
        {
            Test->add_result(1, "Error opening query file");
        }

        if (local_result)
        {
            Test->add_result(1, "********** rules%d test FAILED", i);
        }
        else
        {
            Test->tprintf("********** rules%d test PASSED", i);
        }

        mysql_close(Test->maxscales->conn_rwsplit[0]);
    }

    Test->set_timeout(180);

    // Test for at_times clause
    if (Test->verbose)
    {
        Test->tprintf("Trying at_times clause");
    }
    copy_rules(Test, (char*) "rules_at_time", rules_dir);


    if (Test->verbose)
    {
        Test->tprintf("DELETE quries without WHERE clause will be blocked during the 15 seconds");
        Test->tprintf("Put time to rules.txt: %s", str);
    }
    Test->maxscales->ssh_node_f(0,
                                false,
                                "start_time=`date +%%T`;"
                                "stop_time=` date --date \"now +15 secs\" +%%T`;"
                                "%s sed -i \"s/###time###/$start_time-$stop_time/\" %s/rules/rules.txt",
                                Test->maxscales->access_sudo[0],
                                Test->maxscales->access_homedir[0]);

    Test->maxscales->restart_maxscale(0);
    Test->maxscales->connect_rwsplit(0);

    Test->tprintf("Trying 'DELETE FROM t1' and expecting FAILURE");
    execute_query_silent(Test->maxscales->conn_rwsplit[0], "DELETE FROM t1");

    if (mysql_errno(Test->maxscales->conn_rwsplit[0]) != 1141)
    {
        Test->add_result(1,
                         "Query succeded, but fail expected, errono is %d",
                         mysql_errno(Test->maxscales->conn_rwsplit[0]));
    }

    Test->tprintf("Waiting 16 seconds and trying 'DELETE FROM t1', expecting OK");

    Test->stop_timeout();
    sleep(16);
    Test->set_timeout(180);
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DELETE FROM t1");

    mysql_close(Test->maxscales->conn_rwsplit[0]);
    Test->maxscales->stop_maxscale(0);

    Test->tprintf("Trying limit_queries clause");
    Test->tprintf("Copying rules to Maxscale machine: %s", str);
    copy_rules(Test, (char*) "rules_limit_queries", rules_dir);

    Test->maxscales->start_maxscale(0);
    Test->maxscales->connect_rwsplit(0);

    Test->tprintf("Trying 10 quries as fast as possible");
    for (i = 0; i < 10; i++)
    {
        Test->add_result(execute_query_silent(Test->maxscales->conn_rwsplit[0], "SELECT * FROM t1"),
                         "%d -query failed",
                         i);
    }

    Test->tprintf("Expecting failures during next 5 seconds");

    time_t start_time_clock = time(NULL);
    timeval t1, t2;
    double elapsedTime;
    gettimeofday(&t1, NULL);

    do
    {
        gettimeofday(&t2, NULL);
        elapsedTime = (t2.tv_sec - t1.tv_sec);
        elapsedTime += (double) (t2.tv_usec - t1.tv_usec) / 1000000.0;
    }
    while ((execute_query_silent(Test->maxscales->conn_rwsplit[0], "SELECT * FROM t1") != 0)
           && (elapsedTime < 10));

    Test->tprintf("Quries were blocked during %f (using clock_gettime())", elapsedTime);
    Test->tprintf("Quries were blocked during %lu (using time())", time(NULL) - start_time_clock);

    if ((elapsedTime > 6) or (elapsedTime < 4))
    {
        Test->add_result(1, "Queries were blocked during wrong time");
    }

    Test->set_timeout(180);
    Test->tprintf("Trying 12 quries, 1 query / second");
    for (i = 0; i < 12; i++)
    {
        sleep(1);
        Test->add_result(execute_query_silent(Test->maxscales->conn_rwsplit[0], "SELECT * FROM t1"),
                         "query failed");
        if (Test->verbose)
        {
            Test->tprintf("%d ", i);
        }
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
