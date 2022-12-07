/**
 * Runs the MariaDB Connector/NodeJS test suite against MaxScale
 */
#include "connector_common.hh"

int main(int argc, char** argv)
{
    TestConnections test;
    test.reset_timeout(500);
    return run_npm_test(test, argc, argv,
                        "https://github.com/mariadb-corporation/mariadb-connector-nodejs.git",
                        "master", "mariadb-connector-nodejs");
}
