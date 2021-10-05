#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    // The USING PASSWORD syntax for ed25519 was added in 10.4
    TestConnections::require_repl_version("10.4");
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("INSTALL SONAME 'auth_ed25519'");

    auto c = test.repl->get_connection(0);

    if (c.connect()
        && c.query("CREATE USER bob IDENTIFIED VIA ed25519 USING PASSWORD('bob')")
        && c.query("GRANT ALL ON *.* TO bob WITH GRANT OPTION"))
    {
        test.repl->sync_slaves();

        test.maxscale->start();

        // There's a race condition in the connector (CONC-568) that can cause the first connection attempt
        // with a non-default auth plugin to fail. To work around this, we can wait for the monitor which
        // causes a reconnection to occur.
        test.maxscale->wait_for_monitor();

        auto rws = test.maxscale->rwsplit();
        test.expect(rws.connect(), "Failed to connect to readwritesplit: %s", rws.error());
        test.expect(rws.query("SELECT 1"), "Query failed: %s", rws.error());

        c.query("DROP USER bob");
    }
    else
    {
        test.add_failure("Failed to create a user for testing: %s", c.error());
    }

    test.repl->execute_query_all_nodes("UNINSTALL SONAME 'auth_ed25519'");

    return test.global_result;
}
