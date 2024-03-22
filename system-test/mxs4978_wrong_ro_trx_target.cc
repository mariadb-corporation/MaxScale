/*
 * Copyright (c) 2024 MariaDB plc
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

#include <maxtest/testconnections.hh>
#include <sstream>

#define CHECK(expr) if (!(expr)) throw std::runtime_error(#expr);

void run_test(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    CHECK(c.connect());
    CHECK(c.query("START TRANSACTION READ ONLY"));
    auto id = c.field("SELECT @@server_id");
    CHECK(!id.empty());
    CHECK(c.query("COMMIT"));

    // This will cause the connection to the server where the previous read-only transaction was done to be
    // closed as its result differs from the expected one.
    std::ostringstream ss;
    ss << "SET @a=( "
       << "SELECT CASE @@server_id WHEN " << id
       << "  THEN (SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES) "
       << "  ELSE 1 "
       << "END"
       << ")";
    CHECK(c.query(ss.str()));

    sleep(1);

    // This will be routed to the server that just failed
    CHECK(c.query("START TRANSACTION READ ONLY"));
    CHECK(c.query("SELECT 1"));
    CHECK(c.query("COMMIT"));
}

void test_main(TestConnections& test)
{
    try
    {
        run_test(test);
    }
    catch (const std::exception& e)
    {
        test.add_failure("%s", e.what());
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
