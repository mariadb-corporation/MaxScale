/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/blob_test.hh>
#include <numeric>

bool test_longblob(TestConnections& test, MYSQL* conn, const char* blob_name,
                   unsigned long chunk_size, int chunks, int rows)
{
    test.try_query(conn, "DROP TABLE IF EXISTS long_blob_table");
    test.tprintf("Creating table with %s", blob_name);
    const char create_fmt[] = "CREATE TABLE long_blob_table(id int NOT NULL AUTO_INCREMENT, x INT, b %s, "
                              "PRIMARY KEY (id))";
    test.try_query(conn, create_fmt, blob_name);

    int err_count = test.logger().m_n_fails;

    for (int k = 0; k < rows; k++)
    {
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        test.expect(stmt, "stmt init error: %s", mysql_error(conn));

        const char insert_stmt[] = "INSERT INTO long_blob_table(x, b) VALUES(1, ?)";
        int res = mysql_stmt_prepare(stmt, insert_stmt, strlen(insert_stmt));
        test.expect(res == 0, "Error preparing stmt: %s", mysql_stmt_error(stmt));

        MYSQL_BIND param[1];
        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].is_null = 0;

        res = mysql_stmt_bind_param(stmt, param);
        test.expect(res == 0, "Error binding parameter: %s", mysql_stmt_error(stmt));

        if (test.logger().m_n_fails == err_count)
        {
            test.tprintf("Filling buffer...");
            size_t total_size = chunk_size * chunks;
            std::vector<uint8_t> data(total_size, 0);
            std::iota(data.begin(), data.end(), 0);

            test.tprintf("Sending data in %i %lu byte chunks, for a total of %lu bytes",
                         chunks, chunk_size, total_size);

            auto ptr = data.data();
            for (int i = 0; i < chunks; i++)
            {
                res = mysql_stmt_send_long_data(stmt, 0, (const char*)ptr, chunk_size);
                if (res)
                {
                    test.add_failure("Error inserting data, chunk %d, error %s", i,
                                     mysql_stmt_error(stmt));
                    break;
                }
                ptr += chunk_size;
            }

            res = mysql_stmt_execute(stmt);
            if (res == 0)
            {
                test.tprintf("Row %i complete.", k);
            }
            else
            {
                test.add_failure("INSERT Statement with %s failed. Error: %s",
                                 blob_name, mysql_stmt_error(stmt));
            }
        }

        test.expect(mysql_stmt_close(stmt) == 0, "Error closing stmt.");
    }

    bool rval = false;
    if (test.logger().m_n_fails == err_count)
    {
        test.tprintf("%s insert success.", blob_name);
        rval = true;
    }
    else
    {
        test.tprintf("%s insert failed.", blob_name);
    }
    return rval;
}

int check_longblob_data(TestConnections& Test, MYSQL* conn, size_t chunk_size, size_t chunks, int rows)
{
    const char select_stmt[] = "SELECT id, x, b FROM long_blob_table ";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    Test.expect(stmt, "stmt init error: %s", mysql_error(conn));

    int res = mysql_stmt_prepare(stmt, select_stmt, strlen(select_stmt));
    Test.expect(res == 0, "Error preparing stmt: %s", mysql_stmt_error(stmt));

    MYSQL_BIND param[1], result[3];
    memset(param, 0, sizeof(param));
    memset(result, 0, sizeof(result));

    int id = 1;
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &id;

    int r_id;
    int r_x;
    unsigned long l_id;
    unsigned long l_x;
    my_bool b_id;
    my_bool b_x;
    my_bool e_id;
    my_bool e_x;

    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &r_id;
    result[0].buffer_length = 0;
    result[0].length = &l_id;
    result[0].is_null = &b_id;
    result[0].error = &e_id;

    result[1].buffer_type = MYSQL_TYPE_LONG;
    result[1].buffer = &r_x;
    result[1].buffer_length = 0;
    result[1].length = &l_x;
    result[1].is_null = &b_x;
    result[1].error = &e_x;

    std::vector<uint8_t> data;
    data.resize(chunk_size * chunks);

    result[2].buffer_type = MYSQL_TYPE_LONG_BLOB;
    result[2].buffer = data.data();
    result[2].buffer_length = chunk_size * chunks;

    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        Test.tprintf("Could not bind results: %s", mysql_stmt_error(stmt));
        return 1;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Test.tprintf("Error executing stmt %s", mysql_error(conn));
    }

    if (mysql_stmt_store_result(stmt) != 0)
    {
        Test.tprintf("Could not buffer result set: %s", mysql_stmt_error(stmt));
        return 1;
    }

    int row = 0;
    while (!mysql_stmt_fetch(stmt))
    {
        Test.tprintf("id=%d\tx=%d\n", r_id, r_x);
        Test.expect(r_id == row + 1, "id field is wrong! Expected %d, got %d", row + 1, r_id);

        uint8_t check = 0;
        for (size_t i = 0; i < chunks; i++)
        {
            for (size_t y = 0; y < chunk_size; y++)
            {
                size_t idx = i * chunk_size + y;
                if (data[idx] != check)
                {
                    Test.add_failure("byte %lu: expected %hhu, got %hhu", idx, check, data[idx]);
                    break;
                }

                ++check;
            }
        }

        row++;
    }

    Test.expect(row == rows, "Wrong number of rows in the table! Expected %d, got %d", rows, row);

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return 0;
}
