/**
 * Firewall filter syntax error test
 *
 * Generate various syntax errors and check if they are detected.
 * With every rule file in this test, MaxScale should not start and the error
 * log should contain a message about a syntax error.
 */


#include <iostream>
#include <unistd.h>
#include <linux/limits.h>
#include "testconnections.h"
#include "fw_copy_rules.h"

const char *temp_rules = "rules_tmp.txt";

const char *users_ok[] =
{
    "users %@% match any rules testrule",
    NULL
};

const char *rules_failure[] =
{
    "rule testrule deny nothing",
    "rule testrule deny regex",
    "rule testrule deny columns",
    "rule testrule deny limit_queries",
    "rule testrule deny no-where-clause",
    "rule testrule deny wildcard wildcard",
    "rule testrule deny wildcard rule testrule deny no_where_clause",
    "rule testrule allow anything",
    "rule testrule block",
    "rule deny wildcard",
    "testrule deny wildcard",
    "rule testrule deny wildcard on_queries select | not_select",
    "rule testrule deny wildcard on_queries select|not_select",
    "rule testrule deny wildcard on_queries select |",
    "rule testrule deny wildcard on_queries select|",
    "rule ᐫᐬᐭᐮᐯᐰᐱ deny wildcard on_queries select|",
    NULL
};

void truncate_maxscale_logs(TestConnections& test)
{
    test.maxscales->ssh_node(0, "truncate -s 0 /var/log/maxscale/*", true);
}

void create_rule(const char *rule, const char* user)
{
    FILE *file = fopen(temp_rules, "a");
    fprintf(file, "%s\n", rule);
    fprintf(file, "%s\n", user);
    fclose(file);
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    test.maxscales->stop_maxscale(0);

    for (int i = 0; rules_failure[i]; i++)
    {
        /** Create rule file with syntax error */
        truncate(temp_rules, 0);
        create_rule(rules_failure[i], users_ok[0]);
        char buf[PATH_MAX + 1];
        copy_rules(&test, (char*)temp_rules, (char*)getcwd(buf, sizeof(buf)));

        test.tprintf("Testing rule: %s\n", rules_failure[i]);
        test.add_result(test.maxscales->start_maxscale(0) == 0, "MaxScale should fail to start");
        test.maxscales->stop_maxscale(0);

        /** Check that MaxScale did not start and that the log contains
         * a message about the syntax error. */
        test.check_maxscale_processes(0, 0);
        test.check_log_err(0, "syntax error", true);
        truncate_maxscale_logs(test);
    }

    return test.global_result;
}
