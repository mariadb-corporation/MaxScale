/**
 * @file fwf_reload - Same as fwf but with reloading of rules
 */


#include <iostream>
#include <ctime>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "fw_copy_rules.h"

int main(int argc, char *argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections *Test = new TestConnections(argc, argv);
    char sql[4096];
    char pass_file[4096];
    char deny_file[4096];
    char rules_dir[4096];

    sprintf(rules_dir, "%s/fw/", test_dir);
    int N = 13;
    int i;
    int exit_code;

    Test->maxscales->stop_maxscale(0);
    char first_rule[] = "rules1";
    copy_rules(Test, first_rule, rules_dir);
    Test->maxscales->start_maxscale(0);
    Test->maxscales->connect_rwsplit(0);


    for (i = 1; i <= N; i++)
    {
        char str[1024];
        sprintf(str, "rules%d", i);
        Test->set_timeout(180);
        copy_rules(Test, str, rules_dir);
        Test->maxscales->ssh_node(0, "maxadmin call command dbfwfilter rules/reload Database-Firewall", true);

        int local_result = 0;
        sprintf(pass_file, "%s/fw/pass%d", test_dir, i);
        FILE *file = fopen(pass_file, "r");

        if (file)
        {
            Test->tprintf("********** Trying queries that should be OK ********** \n");

            while (!feof(file))
            {
                Test->set_timeout(180);

                if (execute_query_from_file(Test->maxscales->conn_rwsplit[0], file) == 1)
                {
                    Test->tprintf("Query should succeed: %s\n", sql);
                    local_result++;
                }
            }
            fclose(file);
        }
        else
        {
            Test->add_result(1, "Error opening file '%s': %d, %s\n", pass_file, errno, strerror(errno));
            break;
        }

        sprintf(deny_file, "%s/fw/deny%d", test_dir, i);
        file = fopen(deny_file, "r");

        if (file)
        {
            Test->tprintf("********** Trying queries that should FAIL ********** \n");

            while (!feof(file))
            {
                Test->set_timeout(180);

                int rc = execute_query_from_file(Test->maxscales->conn_rwsplit[0], file);

                if (rc != -1 && (rc == 0 ||
                                 mysql_errno(Test->maxscales->conn_rwsplit[0]) != 1141))
                {
                    Test->tprintf("Query should fail: %s\n", sql);
                    local_result++;
                }
            }

            fclose(file);
        }
        else
        {
            Test->add_result(1, "Error opening file '%s': %d, %s\n", deny_file, errno, strerror(errno));
            break;
        }

        Test->add_result(local_result, "********** rules%d test FAILED\n", i);
    }

    Test->tprintf("Trying rules with syntax error\n");
    copy_rules(Test, (char *) "rules_syntax_error", rules_dir);

    char *output = Test->maxscales->ssh_node_output(0,
                                                    "maxadmin call command dbfwfilter rules/reload Database-Firewall", true, &exit_code);
    Test->add_result(strcasestr(output, "Failed") == NULL, "Reloading rules should fail with syntax errors");

    Test->check_maxscale_processes(0, 1);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
