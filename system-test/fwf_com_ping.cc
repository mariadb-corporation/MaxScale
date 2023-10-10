/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-1111: Dbfwfilter COM_PING test
 *
 * Check that COM_PING is allowed with `action=allow`
 */

#include <maxtest/testconnections.hh>

const char* rules = "rule test1 deny regex '.*'\n"
                    "users %@% match any rules test1\n";

int main(int argc, char** argv)
{
    /** Create the rule file */
    FILE* file = fopen("rules.txt", "w");
    fwrite(rules, 1, strlen(rules), file);
    fclose(file);

    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.maxscale->copy_fw_rules("rules.txt", ".");

    test.maxscale->restart_maxscale();
    test.maxscale->connect_maxscale();
    test.tprintf("Pinging MaxScale, expecting success");
    test.add_result(mysql_ping(test.maxscale->conn_rwsplit),
                    "Ping should not fail: %s",
                    mysql_error(test.maxscale->conn_rwsplit));
    test.maxscale->close_maxscale_connections();

    return test.global_result;
}
