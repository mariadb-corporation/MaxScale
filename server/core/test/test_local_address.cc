/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <maxscale/config.h>
#include <maxscale/mysql_utils.h>

using namespace std;

char USAGE[] =
    "usage: test_local_address -u user [-p password] [-a address] [-h host] [-s success]\n"
    "\n"
    "user    : The user to connect as.\n"
    "password: The password of the user, default none.\n"
    "address : The address to connect from, default none specified.\n"
    "host    : The address of the host to connect to, default 127.0.0.1.\n"
    "success : (0|1), whether the connection attempt is expected to succeed or not, defaul 1.\n"
    "\n"
    "Example:\n"
    "\n"
    "MariaDB [(none)]> create user 'l1'@'192.168.1.254';\n"
    "MariaDB [(none)]> create user 'l2'@'127.0.0.1';\n"
    "\n"
    "$ ./test_local_address -s 1 -u l1 -a 192.168.1.254\n"
    "User    : l1\n"
    "Password: (none)\n"
    "Server  : 127.0.0.1\n"
    "Address : 192.168.1.254\n"
    "Success : 1\n"
    "\n"
    "Could connect, as expected.\n"
    "$ ./test_local_address -s 0 -u l1 -a 127.0.0.1\n"
    "User    : l1\n"
    "Password: (none)\n"
    "Server  : 127.0.0.1\n"
    "Address : 127.0.0.1\n"
    "Success : 0\n"
    "\n"
    "Could not connect, as expected. "
    "Reported error: Access denied for user 'l1'@'localhost' (using password: NO)\n"
    "$ ./test_local_address -s 1 -u l2 -a 127.0.0.1\n"
    "User    : l2\n"
    "Password: (none)\n"
    "Server  : 127.0.0.1\n"
    "Address : 127.0.0.1\n"
    "Success : 1\n"
    "\n"
    "Could connect, as expected.\n"
    "$ ./test_local_address -s 0 -u l2 -a 192.168.1.254\n"
    "User    : l2\n"
    "Password: (none)\n"
    "Server  : 127.0.0.1\n"
    "Address : 192.168.1.254\n"
    "Success : 0\n"
    "\n"
    "Could not connect, as expected. "
    "Reported error: Access denied for user 'l2'@'192.168.1.254' (using password: NO)\n";

namespace
{

int test(bool success, const char* zHost, const char* zUser, const char* zPassword, const char* zAddress)
{
    int rv = EXIT_FAILURE;

    MXS_CONFIG* config = config_get_global_options();
    config->local_address = const_cast<char*>(zAddress);

    SERVER server;
    memset(&server, 0, sizeof(server));

    strcpy(server.address, zHost);
    server.port = 3306;

    MYSQL* pMysql = mysql_init(NULL);

    MYSQL* pConn = mxs_mysql_real_connect(pMysql, &server, zUser, zPassword);

    if (pConn)
    {
        if (success)
        {
            cout << "Could connect, as expected." << endl;
            rv = EXIT_SUCCESS;
        }
        else
        {
            cerr << "Error: Connection succeeded, although expected not to." << endl;
        }

        mysql_close(pConn);
    }
    else
    {
        if (!success)
        {
            cout << "Could not connect, as expected. Reported error: " << mysql_error(pMysql) << endl;
            rv = EXIT_SUCCESS;
        }
        else
        {
            cerr << "Error: " << mysql_error(pMysql) << endl;
        }

        mysql_close(pMysql);
    }

    return rv;
}
}

int main(int argc, char* argv[])
{
    int rv = EXIT_SUCCESS;

    int opt;

    const char* zUser = NULL;
    const char* zPassword = NULL;
    const char* zAddress = NULL;
    const char* zHost = "127.0.0.1";
    bool success = true;

    while ((opt = getopt(argc, argv, "a:h:p:s:u:")) != -1)
    {
        switch (opt)
        {
        case 'a':
            zAddress = optarg;
            break;

        case 'h':
            zHost = optarg;
            break;

        case 'p':
            zPassword = optarg;
            break;

        case 's':
            success = (atoi(optarg) == 0) ? false : true;
            break;

        case 'u':
            zUser = optarg;
            break;

        default:
            rv = EXIT_FAILURE;
        }
    }

    if ((rv == EXIT_SUCCESS) && zUser)
    {
        cout << "User    : " << zUser << endl;
        cout << "Password: " << (zPassword ? zPassword : "(none)") << endl;
        cout << "Server  : " << zHost << endl;
        cout << "Address : " << (zAddress ? zAddress : "(default)") << endl;
        cout << "Success : " << success << endl;
        cout << endl;

        rv = test(success, zHost, zUser, zPassword, zAddress);
    }
    else
    {
        cerr << USAGE << endl;
    }

    return rv;
}
