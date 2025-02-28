/*
 * Copyright (c) 2025 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <sstream>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto c = test.maxscale->rwsplit();
    c.connect();
    c.query("CREATE OR REPLACE TABLE test.t1(id INT)");

    test.repl->execute_query_all_nodes("SET GLOBAL session_track_system_variables='last_gtid'");
    test.repl->execute_query_all_nodes("STOP SLAVE; CHANGE MASTER TO MASTER_DELAY=120; START SLAVE");

    // We need to use Connector/Node.js to be able to test the DEPRECATE_EOF protocol.
    // The Connector/C doesn't support it and thus GTID values from resultsets are
    // not available.
    std::ostringstream ss;
    ss << " cp -t " << mxt::BUILD_DIR << "/nodejs/ " << mxt::SOURCE_DIR << "/nodejs/* && "
       << " cd " << mxt::BUILD_DIR << "/nodejs/ && npm i &&"
       << " MAXSCALE_HOST=" << test.maxscale->ip()
       << " MAXSCALE_PORT=4006"
       << " MAXSCALE_USER=" << test.maxscale->user_name()
       << " MAXSCALE_PASSWORD=" << test.maxscale->password()
       << " MAXSCALE_DB=test"
       << " npm run test";

    int rc = system(ss.str().c_str());

    test.expect(WIFEXITED(rc) && WEXITSTATUS(rc) == 0, "Test failed");

    c.query("DROP TABLE test.t1");
    test.repl->execute_query_all_nodes("STOP SLAVE; CHANGE MASTER TO MASTER_DELAY=0; START SLAVE");

    return test.global_result;
}
