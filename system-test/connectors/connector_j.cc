/**
 * Runs the MariaDB Connector/J test suite against MaxScale
 */
#include "connector_common.hh"

int main(int argc, char** argv)
{
    TestConnections test;
    // The Connector/J test also takes while, give it some extra time to complete.
    test.reset_timeout(500);

    return run_maven_test(test, argc, argv,
                          "https://github.com/mariadb-corporation/mariadb-connector-j",
                          "master", "mariadb-connector-j");
}
