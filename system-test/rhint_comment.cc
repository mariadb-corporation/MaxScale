/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file bug475.cpp regression case for bug 475 (The end comment tag in hints isn't properly detected)
 *
 * Test tries different hints with comments syntax and then checks log and checks if MaxScale is alive
 */

//
// Markus Mäkelä 2014-08-08 10:09:48 UTC
// The closing tag isn't properly detected when using inline comments.
// Multiple commands cause this behaviour. The following commands cause these messages in the error log:

// select /* maxscale hintname prepare route to master */ @@server_id;
// 2014 08/08 13:01:09   Error : Syntax error in hint. Expected 'master', 'slave', or 'server' instead of
// '*/'. Hint ignored.

// select /* maxscale hintname begin */ @@server_id;
// 2014 08/08 13:02:45   Error : Syntax error in hint. Expected '=', 'prepare', or 'start' instead of
// '@@server_id'. Hint ignored.

// The following only happens when no whitespace is used after 'master' and '*/':
// select /* maxscale route to master*/ @@server_id;
// 2014 08/08 13:04:38   Error : Syntax error in hint. Expected 'master', 'slave', or 'server' instead of
// 'master*/'. Hint ignored.

// All other forms of '/* maxscale route to [slave|server <server name>]*/' work even without the whitespace
// before the closing tag.




#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{

    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();

    Test->maxscale->connect_maxscale();

    Test->try_query(Test->maxscale->conn_rwsplit,
                    (char*) "select /* maxscale hintname prepare route to master */ @@server_id;");
    Test->try_query(Test->maxscale->conn_rwsplit,
                    (char*) "select /* maxscale hintname begin */ @@server_id;");
    Test->try_query(Test->maxscale->conn_rwsplit,
                    (char*) "select /* maxscale route to master*/ @@server_id;");

    Test->log_excludes("Syntax error in hint");
    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
