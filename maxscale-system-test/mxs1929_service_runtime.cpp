/**
 * MXS-1929: Runtime service creation
 */
#include "testconnections.h"

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace std;

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    // We need to do this since we don't have maxadmin enabled
    test.maxscales->restart();

    auto maxctrl = [&](string cmd, bool print = true) {
            test.set_timeout(60);
            auto rv = test.maxscales->ssh_output("maxctrl " + cmd);

            if (rv.first != 0 && print)
            {
                cout << "MaxCtrl: " << rv.second << endl;
            }

            return rv.first == 0;
        };

    Connection c1 = test.maxscales->rwsplit();
    string host1 = test.repl->IP[0];
    string port1 = to_string(test.repl->port[0]);
    string host2 = test.repl->IP[1];
    string port2 = to_string(test.repl->port[1]);
    string host3 = test.repl->IP[2];
    string port3 = to_string(test.repl->port[2]);

    cout << "Create a service and check that it works" << endl;

    maxctrl("create service svc1 readwritesplit user=skysql password=skysql");

    maxctrl("create listener svc1 listener1 4006");
    maxctrl("create monitor mon1 mariadbmon --monitor-user skysql --monitor-password skysql");
    maxctrl("create server server1 " + host1 + " " + port1 + " --services svc1 --monitors mon1");
    maxctrl("create server server2 " + host2 + " " + port2 + " --services svc1 --monitors mon1");
    maxctrl("create server server3 " + host3 + " " + port3 + " --services svc1 --monitors mon1");

    c1.connect();
    test.assert(c1.query("SELECT 1"), "Query to simple service should work: %s", c1.error());
    c1.disconnect();

    cout << "Destroy the service and check that it is removed" << endl;

    test.assert(!maxctrl("destroy service svc1", false), "Destroying linked service should fail");
    maxctrl("unlink service svc1 server1 server2 server3");
    test.assert(!maxctrl("destroy service svc1", false),
                "Destroying service with active listeners should fail");
    maxctrl("destroy listener svc1 listener1");
    test.assert(maxctrl("destroy service svc1"), "Destroying valid service should work");

    test.set_timeout(60);
    test.assert(!c1.connect(), "Connection should be rejected");
    test.stop_timeout();

    cout << "Create the same service again and check that it still works" << endl;

    maxctrl("create service svc1 readwritesplit user=skysql password=skysql");
    maxctrl("create listener svc1 listener1 4006");
    maxctrl("link service svc1 server1 server2 server3");

    c1.connect();
    test.assert(c1.query("SELECT 1"), "Query to recreated service should work: %s", c1.error());
    c1.disconnect();

    cout << "Check that active connections aren't closed when service is destroyed" << endl;

    c1.connect();
    maxctrl("unlink service svc1 server1 server2 server3");
    maxctrl("destroy listener svc1 listener1");
    maxctrl("destroy service svc1");

    test.assert(c1.query("SELECT 1"), "Query to destroyed service should still work");

    // Start a thread to attempt a connection before the last connection
    // is closed. The connection attempt should be rejected when the
    // listener is freed.
    mutex m;
    condition_variable cv;
    thread t([&]() {
                 cv.notify_one();
                 test.assert(!test.maxscales->rwsplit().connect(),
                             "New connections to created service "
                             "should fail with a timeout while the original connection is open");
             });

    // Wait until the thread starts
    unique_lock<mutex> ul(m);
    cv.wait(ul);
    ul.unlock();

    // This is unreliable but it's adequate for testing to ensure a connection
    // is opened before the old one is closed
    sleep(1);

    test.set_timeout(60);

    // Disconnect the original connection and try to reconnect
    c1.disconnect();
    test.assert(!c1.connect(), "New connections should be rejected after original connection is closed");

    // The connection should be rejected once the last connection is closed. If
    // it doesn't, we hit the test timeout before the connection timeout.
    t.join();

    test.stop_timeout();

    return test.global_result;
}
