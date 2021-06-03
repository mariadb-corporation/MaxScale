/**
 * @file mxs1110_16mb.cpp - trying to use LONGBLOB with > 16 mb data blocks
 * - try to insert large LONGBLOB via RWSplit in blocks > 16mb
 * - read data via RWsplit, ReadConn master, ReadConn slave, compare with inserted data
 */

#include <maxtest/testconnections.hh>
#include <maxtest/blob_test.hh>
#include <maxtest/galera_cluster.hh>

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections::require_galera(true);
    TestConnections* Test = new TestConnections(argc, argv);
    Test->maxscale->stop();
    Test->set_timeout(60);
    int chunk_size = 2500000;
    int chunk_num = 5;
    std::string src_dir = test_dir;
    std::string masking_rules = src_dir + "/masking/masking_user/masking_rules.json";
    std::string cache_rules = src_dir + "/cache/cache_basic/cache_rules.json";
    std::string fw_rules = src_dir + "/fw";

    Test->maxscale->copy_to_node(masking_rules.c_str(), "~/");

    Test->maxscale->copy_to_node(cache_rules.c_str(), "~/");

    Test->maxscale->ssh_node("chmod a+rw *.json", true);

    Test->maxscale->copy_fw_rules("rules2", fw_rules);

    Test->maxscale->start_maxscale();

    Test->repl->execute_query_all_nodes((char*) "set global max_allowed_packet=200000000");
    Test->galera->execute_query_all_nodes((char*) "set global max_allowed_packet=200000000");

    Test->maxscale->connect_maxscale();
    Test->repl->connect();
    Test->tprintf("LONGBLOB: Trying send data via RWSplit\n");
    test_longblob(Test, Test->maxscale->conn_rwsplit[0], (char*) "LONGBLOB", chunk_size, chunk_num, 2);
    Test->repl->close_connections();
    Test->maxscale->close_maxscale_connections();

    Test->repl->sync_slaves();
    Test->maxscale->connect_maxscale();
    Test->tprintf("Checking data via RWSplit\n");
    check_longblob_data(Test, Test->maxscale->conn_rwsplit[0], chunk_size, chunk_num, 2);
    Test->tprintf("Checking data via ReadConn master\n");
    check_longblob_data(Test, Test->maxscale->conn_master, chunk_size, chunk_num, 2);
    Test->tprintf("Checking data via ReadConn slave\n");
    check_longblob_data(Test, Test->maxscale->conn_slave, chunk_size, chunk_num, 2);
    Test->maxscale->close_maxscale_connections();

    MYSQL* conn_galera = open_conn(4016,
                                   Test->maxscale->ip4(),
                                   Test->maxscale->user_name,
                                   Test->maxscale->password,
                                   Test->maxscale_ssl);
    mysql_close(conn_galera);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
