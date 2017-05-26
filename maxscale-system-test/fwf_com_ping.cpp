/**
 * MXS-1111: Dbfwfilter COM_PING test
 *
 * Check that COM_PING is allowed with `action=allow`
 */

#include "testconnections.h"

const char *rules = "rule test1 deny regex '.*'\n"
                    "users %@% match any rules test1\n";

int main(int argc, char** argv)
{
    /** Create the rule file */
    FILE *file = fopen("rules.txt", "w");
    fwrite(rules, 1, strlen(rules), file);
    fclose(file);

    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.ssh_maxscale(true, "mkdir -p /home/vagrant/rules/; chown -R vagrant:vagrant /home/vagrant/rules/");
    test.copy_to_maxscale((char*)"rules.txt", (char*)"~/rules/rules.txt");
    test.ssh_maxscale(true, "chmod a+r /home/vagrant/rules/rules.txt;");

    test.restart_maxscale();
    test.connect_maxscale();
    test.tprintf("Pinging MaxScale, expecting success");
    test.add_result(mysql_ping(test.conn_rwsplit), "Ping should not fail: %s", mysql_error(test.conn_rwsplit));
    test.close_maxscale_connections();

    return test.global_result;
}
