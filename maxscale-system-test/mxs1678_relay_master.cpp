/**
 * MXS-1678: Stopping IO thread on relay master causes it to be promoted as master
 *
 * https://jira.mariadb.org/browse/MXS-1678
 */
#include "testconnections.h"
#include <set>
#include <string>

typedef std::set<std::string> StringSet;

// Note: This is backported from 2.2 and can be replaced with MaxScales::get_server_status once merged
StringSet state(TestConnections& test, const char* name)
{
    StringSet rval;
    char* res = test.ssh_maxscale_output(true, "maxadmin list servers|grep \'%s\'", name);
    char* pipe = strrchr(res, '|');

    if (res && pipe)
    {
        pipe++;
        char* tok = strtok(pipe, ",");

        while (tok)
        {
            char* p = tok;
            char *end = strchr(tok, '\n');
            if (!end)
            {
                end = strchr(tok, '\0');
            }

            // Trim leading whitespace
            while (p < end && isspace(*p))
            {
                p++;
            }

            // Trim trailing whitespace
            while (end > tok && isspace(*end))
            {
                *end-- = '\0';
            }

            rval.insert(p);
            tok = strtok(NULL, ",\n");
        }
    }

    free(res);

    return rval;
}


int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    execute_query(test.repl->nodes[3], "STOP SLAVE");
    execute_query(test.repl->nodes[3], "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d",
                  test.repl->IP_private[2], test.repl->port[2]);
    execute_query(test.repl->nodes[3], "START SLAVE");

    StringSet master = {"Master", "Running"};
    StringSet slave = {"Slave", "Running"};
    StringSet relay_master = {"Relay Master", "Slave", "Running"};

    test.tprintf("Checking before stopping IO thread");
    char *output = test.ssh_maxscale_output(true, "maxadmin list servers");
    test.tprintf("%s", output);
    free(output);

    test.add_result(state(test, "server1") != master, "server1 is not a master");
    test.add_result(state(test, "server2") != slave, "server2 is not a slave");
    test.add_result(state(test, "server3") != relay_master, "server3 is not a relay master");
    test.add_result(state(test, "server4") != slave, "server4 is not a slave");

    execute_query(test.repl->nodes[2], "STOP SLAVE IO_THREAD");
    sleep(10);

    test.tprintf("Checking after stopping IO thread");
    output = test.ssh_maxscale_output(true, "maxadmin list servers");
    test.tprintf("%s", output);
    free(output);
    test.add_result(state(test, "server1") != master, "server1 is not a master");
    test.add_result(state(test, "server2") != slave, "server2 is not a slave");
    test.add_result(state(test, "server3") != relay_master, "server3 is not a relay master");
    test.add_result(state(test, "server4") != slave, "server4 is not a slave");

    test.repl->fix_replication();
    return test.global_result;
}
