/**
 * @file mxs951_utfmb4.cpp Set utf8mb4 in the backend and restart Maxscale
 * - add following to backend server configuration:
 *  @verbatim
 *  [mysqld]
 *  character_set_server=utf8mb4
 *  collation_server=utf8mb4_unicode_520_ci
 *  @endverbatim
 * - for all backend nodes: SET GLOBAL character_set_server = 'utf8mb4'; SET NAMES 'utf8mb4'
 * - restart Maxscale
 * - connect to Maxscale
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.stop_timeout();
    test.stop_maxscale();

    auto repl = test.repl;
    auto N = repl->N;

    string source_file = mxb::string_printf("%s/utf64.cnf", test_dir);
    for (int i = 0; i < N; i++)
    {
        repl->copy_to_node(i, source_file.c_str(), "./");
        repl->ssh_node(i, "cp ./utf64.cnf /etc/my.cnf.d/", true);
    }

    repl->stop_nodes();
    repl->start_nodes();
    test.start_maxscale();
    test.maxscales->wait_for_monitor(1);

    test.tprintf("Set utf8mb4 for backend");
    repl->execute_query_all_nodes("SET GLOBAL character_set_server = 'utf8mb4';");

    test.tprintf("Set names to utf8mb4 for backend");
    repl->execute_query_all_nodes("SET NAMES 'utf8mb4';");

    test.set_timeout(120);

    test.tprintf("Restart Maxscale");
    test.maxscales->restart_maxscale();
    test.check_maxscale_alive();

    test.stop_timeout();
    test.tprintf("Restore backend configuration\n");

    for (int i = 0; i < N; i++)
    {
        repl->ssh_node(i, "rm  /etc/my.cnf.d/utf64.cnf", true);
    }
    repl->stop_nodes();
    repl->start_nodes();

    return test.global_result;
}
