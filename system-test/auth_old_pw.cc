/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Check that old-style passwords are detected
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE USER 'old'@'%%' IDENTIFIED BY 'old';");
    execute_query(test.repl->nodes[0], "SET PASSWORD FOR 'old'@'%%' = OLD_PASSWORD('old')");
    execute_query(test.repl->nodes[0], "FLUSH PRIVILEGES");
    test.repl->sync_slaves();

    test.tprintf("Trying to connect using user with old style password");

    MYSQL* conn = open_conn(test.maxscale->rwsplit_port,
                            test.maxscale->ip4(),
                            (char*) "old",
                            (char*)  "old",
                            test.maxscale_ssl);
    test.add_result(mysql_errno(conn) == 0, "Connections is open for the user with old style password.\n");
    mysql_close(conn);

    execute_query(test.repl->nodes[0], "DROP USER 'old'@'%%'");

    return test.global_result;
}
