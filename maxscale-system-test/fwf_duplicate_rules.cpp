/**
 * Dbfwfilter duplicate rule test
 *
 * Check if duplicate rules are detected.
 */

#include "testconnections.h"

const char *rules = "rule test1 deny no_where_clause\n"
                    "rule test1 deny columns a b c\n"
                    "users %@% match any rules test1\n";

int main(int argc, char** argv)
{
    /** Create the rule file */
    FILE *file = fopen("rules.txt", "w");
    fwrite(rules, 1, strlen(rules), file);
    fclose(file);

    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.maxscales->ssh_node(0, "mkdir -p /home/vagrant/rules/; chown -R vagrant:vagrant /home/vagrant/rules/",
                             true);
    test.maxscales->copy_to_node((char*)"rules.txt", (char*)"~/rules/rules.txt", 0);
    test.maxscales->ssh_node(0, "chmod a+r /home/vagrant/rules/rules.txt;", true);

    int rc = 0;

    if (test.restart_maxscale() == 0)
    {
        test.tprintf("Restarting MaxScale succeeded when it should've failed!");
        rc = 1;
    }

    return rc;
}
