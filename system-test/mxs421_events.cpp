/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

string get_unique_user()
{
    stringstream ss;
    ss << "mxs421_";
    ss << getpid();
    ss << "_";

    for (int i = 0; i < 2; ++i)
    {
        ss << random();
    }

    return ss.str();
}

void connect_as_user(TestConnections& test, const string& user)
{
    MYSQL* pMysql = mysql_init(NULL);
    test.expect(pMysql, "mysql_init() failed.");

    if (pMysql)
    {
        const char* zHost = test.maxscales->ip4(0);
        int port = test.maxscales->rwsplit_port[0];
        const char* zUser = user.c_str();
        const char* zPassword = "nonexistent";

        mysql_real_connect(pMysql, zHost, zUser, zPassword, "test", port, NULL, 0);

        mysql_close(pMysql);
    }
}

bool found_in_file(TestConnections& test, const string& file, const string& pattern)
{
    string command;
    command += "fgrep ";
    command += pattern;
    command += " ";
    command += file;

    return test.maxscales->ssh_node_f(0, true, "%s", command.c_str()) == 0;
}
}

int main(int argc, char* argv[])
{
    srandom(time(NULL));

    TestConnections test(argc, argv);
    string secure_log;
    int rc;

    secure_log = "/var/log/auth.log";
    rc = test.maxscales->ssh_node_f(0, true, "test -f %s", secure_log.c_str());
    if (rc != 0)
    {
        test.tprintf("'/var/log/auth.log` does not exist. trying with '/var/log/secure'");

        secure_log = "/var/log/secure";
        rc = test.maxscales->ssh_node_f(0, true, "test -f %s", secure_log.c_str());
    }

    if (rc != 0)
    {
        test.tprintf("Neither '/var/log/auth.log`, nor '/var/log/secure' exists, failing.");
        return 1;
    }

    // Ensure that non-root programs can log to the authentication log.
    rc = test.maxscales->ssh_node_f(0, true,
                                    "echo 'auth,authpriv.*  %s' > /etc/rsyslog.d/99-maxscale.conf; "
                                    "service rsyslog restart", secure_log.c_str());

    if (rc != 0)
    {
        test.tprintf("Could not add /etc/rsyslog.d/99-maxscale.conf or not restart rsyslog. Test may fail.");
    }

    test.maxscales->connect();

    string user;

    // Connect with an invalid user.
    user = get_unique_user();
    cout << "user: " << user << endl;
    connect_as_user(test, user);
    sleep(2);
    // There should be an error in maxscale.log
    test.log_includes(0, user.c_str());
    // But not in the authentication log.
    test.expect(!found_in_file(test, secure_log.c_str(), user),
                "Unexpectedly found %s in %s",
                user.c_str(),
                secure_log.c_str());

    // Turn on 'event.authentication_failure.facility=LOG_AUTH'
    test.maxscales->stop();
    test.maxscales->ssh_node_f(0, true, "sed -i 's/#event/event/' /etc/maxscale.cnf");
    test.maxscales->start();

    // Connect again. This should cause an error to be logged to the authentication log.
    user = get_unique_user();
    cout << "user: " << user << endl;
    connect_as_user(test, user);
    sleep(2);

    // There should be an error in maxscale.log, as maxlog is not affected by the syslog setting.
    test.log_includes(0, user.c_str());
    // And in the authentication log as that's where authentication errors now should go.
    test.expect(found_in_file(test, secure_log.c_str(), user),
                "Unexpectedly NOT found %s in %s",
                user.c_str(),
                secure_log.c_str());

    return test.global_result;
}
