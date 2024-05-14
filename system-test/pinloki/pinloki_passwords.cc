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
#include "pinloki_select_master.hh"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    // Create new encryption keys
    auto rv = test.maxscale->ssh_output("maxkeys");
    test.expect(rv.rc == 0, "maxkeys failed: %s", rv.output.c_str());

    // Encrypt the password
    rv = test.maxscale->ssh_output("maxpasswd skysql");
    test.expect(rv.rc == 0, "maxpasswd failed: %s", rv.output.c_str());

    // Replace the passwords with the encrypted ones
    test.maxscale->ssh_output(
        "sed -i 's/password=wrong_password/password=" + rv.output + "/' /etc/maxscale.cnf");
    test.maxscale->start();
    test.maxscale->wait_for_monitor(2);

    return MasterSelectTest(test).result();
}
