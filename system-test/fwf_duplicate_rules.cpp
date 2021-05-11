/**
 * Dbfwfilter duplicate rule test
 *
 * Check if duplicate rules are detected.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/fw_copy_rules.hh>

const char* rules = "rule test1 deny no_where_clause\n"
                    "rule test1 deny columns a b c\n"
                    "users %@% match any rules test1\n";

int main(int argc, char** argv)
{
    /** Create the rule file */
    FILE* file = fopen("rules.txt", "w");
    fwrite(rules, 1, strlen(rules), file);
    fclose(file);

    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    copy_rules(&test, (char*) "rules.txt", (char*) ".");

    int rc = 0;

    if (test.maxscales->restart_maxscale() == 0)
    {
        test.tprintf("Restarting MaxScale succeeded when it should've failed!");
        rc = 1;
    }

    return rc;
}
