/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "testconnections.h"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <vector>
#include <string>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

using namespace std;

namespace
{

template<class T>
void to_collection(string s, const string& delimiter, T* pT)
{
    size_t pos;

    while ((pos = s.find(delimiter)) != std::string::npos)
    {
        pT->push_back(s.substr(0, pos));
        s.erase(0, pos + delimiter.length());
    }

    if (s.length() != 0)
    {
        pT->push_back(s);
    }
}

string& ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                    std::not1(std::ptr_fun<int, int>(std::isspace))));
    return s;
}

string& rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    return s;
}

string& trim(std::string& s)
{
    return ltrim(rtrim(s));
}

string extract_ip(string s)
{
    // 's' looks something like: "    inet 127.0.0.1/...";
    s = s.substr(9); // => "127.0.0.1/...";
    s = s.substr(0, s.find_first_of('/')); // => "127.0.0.1"
    return s;
}

void get_maxscale_ips(TestConnections& test, vector<string>* pIps)
{
    int exit_code;
    string output(test.maxscales->ssh_node_output(0, "ip addr|fgrep inet|fgrep -v ::", false, &exit_code));

    to_collection(output, "\n", pIps);
    transform(pIps->begin(), pIps->end(), pIps->begin(), extract_ip);

    pIps->erase(find(pIps->begin(), pIps->end(), "127.0.0.1"));
}

}

namespace
{

void drop_user(TestConnections& test, const string& user, const string& host)
{
    string stmt("DROP USER IF EXISTS ");

    stmt += "'";
    stmt += user;
    stmt += "'@'";
    stmt += host;
    stmt += "'";
    test.try_query(test.maxscales->conn_rwsplit[0], stmt.c_str());
}

void create_user(TestConnections& test, const string& user, const string& password, const string& host)
{
    string stmt("CREATE USER ");

    stmt += "'";
    stmt += user;
    stmt += "'@'";
    stmt += host;
    stmt += "'";
    stmt += " IDENTIFIED BY ";
    stmt += "'";
    stmt += password;
    stmt += "'";
    test.try_query(test.maxscales->conn_rwsplit[0], stmt.c_str());
}

void grant_access(TestConnections& test, const string& user, const string& host)
{
    string stmt("GRANT SELECT, INSERT, UPDATE ON *.* TO ");

    stmt += "'";
    stmt += user;
    stmt += "'@'";
    stmt += host;
    stmt += "'";
    test.try_query(test.maxscales->conn_rwsplit[0], stmt.c_str());

    test.try_query(test.maxscales->conn_rwsplit[0], "FLUSH PRIVILEGES");
}

void create_user_and_grants(TestConnections& test,
                            const string& user, const string& password, const string& host)
{
    test.tprintf("Creating user: %s@%s", user.c_str(), host.c_str());

    drop_user(test, user, host);
    create_user(test, user, password, host);
    grant_access(test, user, host);
}

bool select_user(MYSQL* pMysql, string* pUser)
{
    bool rv = false;

    if (mysql_query(pMysql, "SELECT USER()") == 0)
    {
        MYSQL_RES* pRes = mysql_store_result(pMysql);

        if (mysql_num_rows(pRes) == 1)
        {
            MYSQL_ROW row = mysql_fetch_row(pRes);
            *pUser = row[0];
            rv = true;
        }

        mysql_free_result(pRes);

        while (mysql_next_result(pMysql) == 0)
        {
            MYSQL_RES* pRes = mysql_store_result(pMysql);
            mysql_free_result(pRes);
        }
    }

    return rv;
}

bool can_connect_to_maxscale(const char* zHost, int port, const char* zUser, const char* zPassword)
{
    bool could_connect = false;

    MYSQL* pMysql = mysql_init(NULL);

    if (pMysql)
    {
        unsigned int timeout = 5;
        mysql_options(pMysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        mysql_options(pMysql, MYSQL_OPT_READ_TIMEOUT, &timeout);
        mysql_options(pMysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout);

        if (mysql_real_connect(pMysql, zHost, zUser, zPassword, NULL, port, NULL, 0))
        {
            string user;
            if (select_user(pMysql, &user))
            {
                could_connect = true;
            }
            else
            {
                cout << "Could not 'SELECT USER()' as '" << zUser << "': " <<  mysql_error(pMysql) << endl;
            }
        }
        else
        {
            cout << "Could not connect as '" << zUser << "': " <<  mysql_error(pMysql) << endl;
        }

        mysql_close(pMysql);
    }

    return could_connect;
}

string get_local_ip(TestConnections& test)
{
    int exit_code;
    string output(test.maxscales->ssh_node_output(0, "nslookup maxscale|fgrep Server:|sed s/Server://", false, &exit_code));
    return trim(output);
}

void start_maxscale_with_local_address(TestConnections& test,
                                       const string& replace,
                                       const string& with)
{
    string command("sed -i s/");
    command += replace;
    command += "/";
    command += with;
    command += "/ ";
    command += "/etc/maxscale.cnf";

    test.maxscales->ssh_node(0, command.c_str(), true);

    test.start_maxscale();
}

void test_connecting(TestConnections& test,
                     const char* zUser, const char* zPassword, const char* zHost,
                     bool should_be_able_to)
{
    bool could_connect = can_connect_to_maxscale(test.maxscales->IP[0], test.maxscales->rwsplit_port[0], zUser, zPassword);

    if (!could_connect && should_be_able_to)
    {
        test.assert(false, "%s@%s should have been able to connect, but wasn't.", zUser, zHost);
    }
    else if (could_connect && !should_be_able_to)
    {
        test.assert(false, "%s@%s should NOT have been able to connect, but was.", zUser, zHost);
    }
    else
    {
        if (could_connect)
        {
            test.tprintf("%s@%s could connect, as expected.", zUser, zHost);
        }
        else
        {
            test.tprintf("%s@%s could NOT connect, as expected.", zUser, zHost);
        }
    }
}

void run_test(TestConnections& test, const string& ip1, const string& ip2)
{
    test.maxscales->connect();

    string local_ip = get_local_ip(test);

    const char* zUser1 = "alice";
    const char* zUser2 = "bob";
    const char* zPassword1 = "alicepwd";
    const char* zPassword2 = "bobpwd";

    create_user_and_grants(test, zUser1, zPassword1, ip1);
    create_user_and_grants(test, zUser1, zPassword1, local_ip);
    create_user_and_grants(test, zUser2, zPassword2, ip2);
    create_user_and_grants(test, zUser2, zPassword2, local_ip);

    test.tprintf("\n");
    test.tprintf("Testing default; alice should be able to access, bob not.");

    test_connecting(test, zUser1, zPassword1, ip1.c_str(), true);
    test_connecting(test, zUser2, zPassword2, ip2.c_str(), false);

    test.maxscales->disconnect();
    test.stop_maxscale();

    test.tprintf("\n");
    test.tprintf("Testing with local_address=%s; alice should be able to access, bob not.",
                 ip1.c_str());

    string local_address_ip1 = "local_address=" + ip1;
    start_maxscale_with_local_address(test, "###local_address###", local_address_ip1);
    test.maxscales->connect();

    test_connecting(test, zUser1, zPassword1, ip1.c_str(), true);
    test_connecting(test, zUser2, zPassword2, ip2.c_str(), false);

    test.maxscales->disconnect();
    test.stop_maxscale();

    test.tprintf("\n");
    test.tprintf("WARNING: Other IP-address not tested, as usable IP-address not available.");

#ifdef USABLE_SECOND_IP_ADDRESS_ON_MAXSCALE_NODE_IS_AVAILABLE
    test.tprintf("\n");
    test.tprintf("\nTesting with local_address=%s, bob should be able to access, alice not.",
                 ip2.c_str());

    string local_address_ip2 = "local_address=" + ip2;
    start_maxscale_with_local_address(test, local_address_ip1, local_address_ip2);
    test.connect_maxscale();

    test_connecting(test, zUser1, zPassword1, ip1.c_str(), false);
    test_connecting(test, zUser2, zPassword2, ip2.c_str(), true);

    test.maxscales->disconnect();
    test.stop_maxscale();
#endif
}

}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    vector<string> ips;
    get_maxscale_ips(test, &ips);

    if (ips.size() >= 2)
    {
        run_test(test, ips[0], ips[1]);
    }
    else
    {
        test.assert(false, "MaxScale node does not have at least two IP-addresses.");
    }

    return test.global_result;
}
