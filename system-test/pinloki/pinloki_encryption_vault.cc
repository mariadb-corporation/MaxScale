/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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

#include "pinloki_encryption.hh"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/install_vault.sh"s, "~/install_vault.sh");
    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/start_vault.sh"s, "~/start_vault.sh");
    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/stop_vault.sh"s, "~/stop_vault.sh");

    auto ret = test.maxscale->ssh_output("./install_vault.sh", false);
    test.expect(ret.rc == 0, "Failed to install Vault: %s", ret.output.c_str());
    ret = test.maxscale->ssh_output("./start_vault.sh", false);
    test.expect(ret.rc == 0, "Failed to start Vault: %s", ret.output.c_str());

    test.maxscale->start();
    auto rv = EncryptionTest(test).result();

    ret = test.maxscale->ssh_output("./stop_vault.sh", false);
    test.expect(ret.rc == 0, "Failed to stop Vault: %s", ret.output.c_str());
    return rv;
}
