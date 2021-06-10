/**
 * @file fwf_reload - Same as fwf but with reloading of rules
 */


#include <iostream>
#include <ctime>
#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections* Test = new TestConnections(argc, argv);
    char sql[4096];
    char pass_file[4096];
    char deny_file[4096];
    char rules_dir[4096];
    auto test_dir = mxt::SOURCE_DIR;

    sprintf(rules_dir, "%s/fw/", test_dir);
    int N = 13;
    int i;
    int exit_code;
    auto mxs = Test->maxscale;

    Test->maxscale->stop();
    char first_rule[] = "rules1";
    mxs->copy_fw_rules(first_rule, rules_dir);
    Test->maxscale->start_maxscale();
    Test->maxscale->connect_rwsplit();


    for (i = 1; i <= N; i++)
    {
        char str[1024];
        sprintf(str, "rules%d", i);
        Test->reset_timeout();
        mxs->copy_fw_rules(str, rules_dir);
        Test->maxctrl("call command dbfwfilter rules/reload Database-Firewall");

        int local_result = 0;
        sprintf(pass_file, "%s/fw/pass%d", test_dir, i);
        FILE* file = fopen(pass_file, "r");

        if (file)
        {
            Test->tprintf("********** Trying queries that should be OK ********** \n");

            while (!feof(file))
            {
                Test->reset_timeout();

                if (execute_query_from_file(Test->maxscale->conn_rwsplit[0], file) == 1)
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
                Test->reset_timeout();

                int rc = execute_query_from_file(Test->maxscale->conn_rwsplit[0], file);

                if (rc != -1 && (rc == 0
                                 || mysql_errno(Test->maxscale->conn_rwsplit[0]) != 1141))
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
    mxs->copy_fw_rules("rules_syntax_error", rules_dir);

    auto res = Test->maxscale->ssh_output("maxctrl call command dbfwfilter rules/reload Database-Firewall");
    Test->add_result(strcasestr(res.output.c_str(), "Failed") == nullptr,
                     "Reloading rules should fail with syntax errors");

    Test->maxscale->expect_running_status(true);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
