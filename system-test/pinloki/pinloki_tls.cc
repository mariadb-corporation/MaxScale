/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "test_base.hh"

using namespace std::literals::string_literals;

class TlsTest : public TestCase
{
public:
    using TestCase::TestCase;

    void setup() override
    {
        master.ssl(true);
        slave.ssl(true);
        test.expect(maxscale.connect(), "Pinloki connection should work: %s", maxscale.error());
        test.expect(master.connect(), "Master connection should work: %s", master.error());
        test.expect(slave.connect(), "Slave connection should work: %s", slave.error());

        slave.query("STOP SLAVE; RESET SLAVE ALL;");

        auto change_master = change_master_sql(test.repl->ip(0), test.repl->port(0));
        change_master += ", MASTER_SSL=1, MASTER_SSL_CA='"s
            + test.maxscale->access_homedir()
            + "/certs/ca.crt'";

        auto gtid = master.field("SELECT @@gtid_current_pos");
        maxscale.query("SET GLOBAL gtid_slave_pos = '" + gtid + "'");

        test.expect(maxscale.query(change_master), "CHANGE MASTER failed: %s", maxscale.error());
        test.expect(maxscale.query("START SLAVE"), "START SLAVE failed: %s", maxscale.error());
        sync(master, maxscale);

        slave.query(change_master_sql(test.maxscale->ip(), test.maxscale->rwsplit_port));
        slave.query("START SLAVE");
        sync(maxscale, slave);
    }

    void run() override
    {
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", maxscale.error());
        test.expect(master.query("INSERT INTO test.t1 VALUES(1)"), "INSERT failed: %s", maxscale.error());
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", maxscale.error());
        sync_all();
        check_gtid();

        // MXS-4096: SSL values in SHOW SLAVE STATUS are empty
        auto c = test.maxscale->open_rwsplit_connection2();

        for (auto query : {"SHOW SLAVE STATUS", "SHOW ALL SLAVES STATUS"})
        {
            auto res = c->query(query);
            test.expect(res.get(), "'%s' failed: %s", query, c->error());
            test.expect(res->next_row(), "'%s' should have one row", query);

            auto ssl = res->get_string("Master_SSL_Allowed");
            auto ca = res->get_string("Master_SSL_CA_File");

            test.expect(ssl == "Yes", "%s: Master_SSL_Allowed should be Yes not %s", query, ssl.c_str());
            test.expect(!ca.empty(), "%s: Master_SSL_CA_File should not be empty.", query);
        }

        // Make sure the diagnostics work with SSL enabled
        test.check_maxctrl("show services");
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return TlsTest(test).result();
}
