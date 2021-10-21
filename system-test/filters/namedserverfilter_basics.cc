/**
 * @file namedserverfilter.cpp Namedserverfilter test
 *
 * Check that a readwritesplit service with a namedserverfilter will route a
 * SELECT @@server_id to the correct server. The filter is configured with
 * `match=SELECT` which should match any SELECT query.
 */

#include <maxtest/testconnections.hh>
#include <iostream>

using std::cout;
using IdSet = std::set<int>;

bool check_server_id(MYSQL* conn, const IdSet& allowed_ids);

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    int server_count = test.repl->N;
    if (server_count < 4)
    {
        test.expect(false, "Too few servers.");
        return test.global_result;
    }

    int server_ids[server_count];
    cout << "Server id:s are:";
    for (int i = 0; i < server_count; i++)
    {
        server_ids[i] = test.repl->get_server_id(i);
        cout << " " << server_ids[i];
    }
    cout << ".\n";

    auto maxconn = test.maxscale->open_rwsplit_connection();
    test.try_query(maxconn, "SELECT 1;");
    if (test.ok())
    {
        const char wrong_server[] = "Query went to wrong server.";
        cout << "Testing with all servers on. Select-queries should go to servers " << server_ids[1]
             << " and " << server_ids[2] << ".\n";
        IdSet allowed = {server_ids[1], server_ids[2]};
        // With all servers on, the query should go to either 2 or 3. Test several times.
        for (int i = 0; i < 5 && test.ok(); i++)
        {
            test.expect(check_server_id(maxconn, allowed), wrong_server);
        }

        auto test_server_down = [&](int node_to_stop, int allowed_node) {
                test.repl->stop_node(node_to_stop);
                test.maxscale->wait_for_monitor(1);
                int stopped_id = server_ids[node_to_stop];
                int allowed_id = server_ids[allowed_node];
                cout << "Stopped server " << stopped_id << ".\n";
                cout << "Select-queries should go to server " << allowed_id << " only.\n";
                IdSet allowed_set = {allowed_id};
                // Test that queries only go to the correct server.
                for (int i = 0; i < 5 && test.ok(); i++)
                {
                    test.expect(check_server_id(maxconn, allowed_set), "%s", wrong_server);
                }
                test.repl->start_node(node_to_stop, "");
                cout << "Restarted server " << stopped_id << ".\n";
            };

        if (test.ok())
        {
            test_server_down(1, 2);
        }
        if (test.ok())
        {
            test_server_down(2, 1);
        }
        if (test.ok())
        {
            test.check_maxctrl("alter filter namedserverfilter target01 server1");
            mysql_close(maxconn);
            maxconn = test.maxscale->open_rwsplit_connection();
            test_server_down(3, 0);
        }
    }
    mysql_close(maxconn);

    test.repl->disconnect();
    return test.global_result;
}

bool check_server_id(MYSQL* conn, const IdSet& allowed_ids)
{
    bool id_ok = false;
    char str[100];
    if (find_field(conn, "SELECT @@server_id", "@@server_id", str))
    {
        cout << "Failed to query for @@server_id: " << mysql_error(conn) << ".\n";
    }
    else
    {
        int queried_id = atoi(str);
        if (allowed_ids.count(queried_id))
        {
            cout << "Query went to server " << queried_id << ".\n";
            id_ok = true;
        }
        else
        {
            cout << "Queried unexpected server id " << queried_id << ".\n";
        }
    }
    return id_ok;
}
