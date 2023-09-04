/**
 * MXS-2441: Add support for read-only slaves to galeramon
 * https://jira.mariadb.org/browse/MXS-2441
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <maxtest/galera_cluster.hh>
#include <maxbase/string.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.repl->connect();
    test.galera->connect();

    for (int i = 0; i < 4; i++)
    {
        test.repl->replicate_from(i, test.galera->ip(0), test.galera->port[0]);
    }

    test.maxscale->wait_for_monitor();

    auto output = mxb::strtok(test.maxctrl("list servers").output, "\n");
    int n_slaves = std::count_if(output.begin(), output.end(), [](const std::string& line) {
                                     return line.find("Slave") != std::string::npos;
                                 });
    int n_masters = std::count_if(output.begin(), output.end(), [](const std::string& line) {
                                      return line.find("Master") != std::string::npos;
                                  });
    int n_synced = std::count_if(output.begin(), output.end(), [](const std::string& line) {
                                     return line.find("Synced") != std::string::npos;
                                 });

    test.expect(n_slaves == 7, "Expected 7 slaves but got %d", n_slaves);
    test.expect(n_masters == 1, "Expected 1 master but got %d", n_masters);
    test.expect(n_synced == 4, "Expected 4 synced but got %d", n_synced);

    // Check that the queries are routed to the right server
    auto repl_ids = test.repl->get_all_server_ids_str();
    auto galera_ids = test.galera->get_all_server_ids_str();
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Could not connect to maxscale: %s", c.error());

    for (int i = 0; i < 4; i++)
    {
        std::string q = "SELECT @@server_id -- maxscale route to server server" + std::to_string(i + 1);
        auto res = c.field(q);
        test.expect(res == repl_ids[i], "Wrong ID: %s(rwsplit) != %s(server)",
                    res.c_str(), repl_ids[i].c_str());
    }

    for (int i = 0; i < 4; i++)
    {
        std::string q = "SELECT @@server_id -- maxscale route to server gserver" + std::to_string(i + 1);
        auto res = c.field(q);
        test.expect(res == galera_ids[i], "Wrong ID: %s(rwsplit) != %s(gserver)",
                    res.c_str(), repl_ids[i].c_str());
    }

    return test.global_result;
}
