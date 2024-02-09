/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Double-close on bad session command result
 */

#include <maxtest/testconnections.hh>

void run_test(TestConnections& test)
{
    Connection conn = test.maxscale->rwsplit();
    conn.connect();

    for (int i = 0; i <= 300 && test.global_result == 0; i++)
    {
        if (conn.query("SET @a = 1")
            && conn.query("USE test")
            && conn.query("SET SQL_MODE=''")
            && conn.query("USE test")
            && conn.query("SELECT @@last_insert_id")
            && conn.query("SELECT 1")
            && conn.query("USE test")
            && conn.query("SELECT 1")
            && conn.query("SET @a = 123")
            && conn.query("BEGIN")
            && conn.query("SELECT @a")
            && conn.query("COMMIT")
            && conn.query("SET @a = 321")
            && conn.query("SELECT @a")
            && conn.query("SET @a = 456")
            && conn.query("START TRANSACTION READ ONLY")
            && conn.query("SELECT @a")
            && conn.query("COMMIT")
            && conn.query("PREPARE ps FROM 'SELECT 1'")
            && conn.query("EXECUTE ps")
            && conn.query("DEALLOCATE PREPARE ps"))
        {
            conn.reset_connection();
        }
        else
        {
            break;
        }
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    std::vector<std::thread> threads;

    for (int i = 0; i < 5; i++)
    {
        threads.emplace_back(run_test, std::ref(test));
    }

    for (int i = 0; i < 5; i++)
    {
        test.repl->stop_node(1 + i % 3);
        test.maxscale->wait_for_monitor();
        test.repl->start_node(1 + i % 3);
        test.maxscale->wait_for_monitor();
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return test.global_result;
}
