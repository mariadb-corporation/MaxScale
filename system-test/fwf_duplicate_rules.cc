/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Dbfwfilter duplicate rule test
 *
 * Check if duplicate rules are detected.
 */

#include <maxtest/testconnections.hh>

const char* rules = "rule test1 deny no_where_clause\n"
                    "rule test1 deny columns a b c\n"
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

    int rc = 0;

    if (test.maxscale->restart_maxscale() == 0)
    {
        test.tprintf("Restarting MaxScale succeeded when it should've failed!");
        rc = 1;
    }

    return rc;
}
