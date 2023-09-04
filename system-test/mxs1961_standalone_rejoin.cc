/**
 * MXS-1961: Standalone master loses master status
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <maxbase/assert.h>

using namespace std;

void checkpoint(TestConnections& test)
{
    for (int i = 0; i < 2; i++)
    {
        sleep(1);
        test.maxscale->wait_for_monitor(1);
    }

    for (auto&& s : {
        "server1", "server2", "server3"
    })
    {
        auto status = test.get_server_status(s);
        cout << s << " { ";
        for (auto a : status)
        {
            cout << a << ", ";
        }
        cout << "}\n";
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto status = [&](const char* server) {
            return test.get_server_status(server);
        };

    auto comment = [&](const char* comment) {
            cout << comment << endl;
        test.maxscale->ssh_node_f(true,
                                  "echo '----- %s -----' >> /var/log/maxscale/maxscale.log", comment);
        };

    auto slave = [&](const char* name) {
            static StringSet slave {"Slave", "Running"};
            test.expect(status(name) == slave, "'%s' should be a slave", name);
        };

    auto master = [&](const char* name) {
            static StringSet master {"Master", "Running"};
            test.expect(status(name) == master, "'%s' should be the master", name);
        };

    auto down = [&](const char* name) {
            static StringSet down {"Down"};
            test.expect(status(name) == down, "'%s' should be down", name);
        };

    auto block = [&](int servernum) {
            mxb_assert(servernum >= 1);
            test.repl->block_node(servernum - 1);
            checkpoint(test);
        };

    auto unblock = [&](int servernum) {
            mxb_assert(servernum >= 1);
            test.repl->unblock_node(servernum - 1);
            checkpoint(test);
        };

    checkpoint(test);

    master("server1");
    slave("server2");
    slave("server3");

    comment("Blocking server1");
    block(1);
    comment("Blocking server2");
    block(2);

    down("server1");
    down("server2");
    master("server3");

    comment("Unblocking server2");
    unblock(2);

    down("server1");
    slave("server2");
    master("server3");

    comment("Blocking server3");
    block(3);
    comment("Unblocking server3");
    unblock(3);

    down("server1");
    master("server2");
    slave("server3");

    comment("Blocking server3");
    block(3);

    down("server1");
    master("server2");
    down("server3");

    comment("Unblocking server1");
    unblock(1);

    slave("server1");
    master("server2");
    down("server3");

    comment("Blocking server2");
    block(2);

    master("server1");
    down("server2");
    down("server3");

    comment("Unblocking server2");
    unblock(2);

    master("server1");
    slave("server2");
    down("server3");

    comment("Unblocking server3");
    unblock(3);

    master("server1");
    slave("server2");
    slave("server3");

    test.maxscale->stop();

    return test.global_result;
}
