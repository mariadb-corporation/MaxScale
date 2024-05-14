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

    // Create the encryption key before MaxScale is started. Use a hard-coded key as the OpenSSL client isn't
    // installed on the test VM.
    const char* key = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    test.maxscale->ssh_node_f(true, "echo -n '1;%s' > /tmp/encryption.key", key);
    test.maxscale->start();

    auto rv = EncryptionTest(test).result();
    test.maxscale->ssh_node_f(true, "rm /tmp/encryption.key");
    return rv;
}
