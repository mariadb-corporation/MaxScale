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

    test.maxscales->ssh_node(0, "mkdir -p /home/vagrant/rules/; chown -R vagrant:vagrant /home/vagrant/rules/",
                             true);
    test.maxscales->copy_to_node((char*)"rules.txt", (char*)"~/rules/rules.txt", 0);
    test.maxscales->ssh_node(0, "chmod a+r /home/vagrant/rules/rules.txt;", true);

    test.maxscales->restart_maxscale(0);
    test.maxscales->connect_maxscale(0);
    test.tprintf("Pinging MaxScale, expecting success");
    test.add_result(mysql_ping(test.maxscales->conn_rwsplit[0]), "Ping should not fail: %s",
                    mysql_error(test.maxscales->conn_rwsplit[0]));
    test.maxscales->close_maxscale_connections(0);

    return test.global_result;
}
