/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include "testconnections.h"

using namespace std;

namespace
{

// Specified in the configuration file.
char USER[] = "maxinfo_user";
char PASSWD[] = "maxinfo_passwd";
int PORT = 4006;

void run(TestConnections& test, MYSQL* pMysql)
{
    if (mysql_query(pMysql, "show eventTimes") == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(pMysql);
        test.expect(pResult, "Executing 'show eventTimes' returned no result.");

        if (pResult)
        {
            int nFields = mysql_field_count(pMysql);
            test.expect(nFields == 3, "Expected 3 fields, got %d.", nFields);

            MYSQL_ROW row;
            while ((row = mysql_fetch_row(pResult)) != NULL)
            {
                cout << row[0] << ", " << row[1] << ", " << row[2] << endl;

                // Right after startup, so all numbers should be small.
                // The regression caused garbage to be returned, so they
                // were all over the place.
                int64_t nEvents_queued = strtoll(row[1], NULL, 0);
                int64_t nEvents_executed = strtoll(row[2], NULL, 0);

                test.expect(nEvents_queued >= 0 && nEvents_queued < 100,
                            "Suspiciously large number of 'No. Events Queued'.");
                test.expect(nEvents_executed >= 0 && nEvents_executed < 100,
                            "Suspiciously large number of 'No. Events Executed'.");
            }

            mysql_free_result(pResult);
        }
    }
    else
    {
        test.expect(false, "Executing 'show eventTimes' failed: %s", mysql_error(pMysql));
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    const char* zMaxScale_host = test.maxscales->ip(0);

    MYSQL* pMysql = open_conn_no_db(PORT, zMaxScale_host, USER, PASSWD);
    test.expect(pMysql, "Could not connect to maxinfo on MaxScale.");

    if (pMysql)
    {
        run(test, pMysql);

        mysql_close(pMysql);
    }

    return test.global_result;
}
