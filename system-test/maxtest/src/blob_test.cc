/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/blob_test.hh>
#include <numeric>

void test_longblob(TestConnections& test, MYSQL* conn,
                   const char* blob_name, unsigned long chunk_size, int chunks, int rows)
{
    const int old_error_count = test.global_result;

    const char tbl_name[] = "test.long_blob_table";
    test.tprintf("Creating table %s with %s", tbl_name, blob_name);
    test.try_query(conn, "DROP TABLE IF EXISTS %s;", tbl_name);
    test.try_query(conn, "CREATE TABLE %s(id int NOT NULL AUTO_INCREMENT, x INT, b %s, PRIMARY KEY (id))",
                   tbl_name, blob_name);

    const char insert_stmt[] = "INSERT INTO test.long_blob_table(x, b) VALUES(1, ?)";
    MYSQL_BIND param[1];

    for (int k = 0; k < rows && (test.global_result == old_error_count); k++)
    {
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (stmt == NULL)
        {
            test.add_failure("stmt init error: %s", mysql_error(conn));
            break;
        }

        int rc = mysql_stmt_prepare(stmt, insert_stmt, strlen(insert_stmt));
        test.expect(rc == 0, "Error preparing stmt: %s", mysql_stmt_error(stmt));

        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].is_null = 0;

        rc = mysql_stmt_bind_param(stmt, param);
        test.expect(rc == 0, "Error binding parameter: %s", mysql_stmt_error(stmt));

        test.tprintf("Filling buffer");
        std::vector<uint8_t> data(chunk_size, 0);
        std::iota(data.begin(), data.end(), 0);

        test.tprintf("Sending data in %lu bytes chunks, %i chunks, total size is %lu",
                     data.size(), chunks, data.size() * chunks);

        for (int i = 0; i < chunks; i++)
        {
            if (mysql_stmt_send_long_data(stmt, 0, (const char*)data.data(), data.size()) != 0)
            {
                test.add_failure("Error inserting data, chunk %d, error %s", i, mysql_stmt_error(stmt));
                break;
            }
        }

        test.tprintf("Executing INSERT for row %i", k);
        rc = mysql_stmt_execute(stmt);
        test.expect(rc == 0, "INSERT Statement with %s failed, error '%s'.", blob_name,
                    mysql_stmt_error(stmt));
        test.expect(mysql_stmt_close(stmt) == 0, "Error closing stmt");
    }

    if (old_error_count == test.global_result)
    {
        test.tprintf("%s is OK\n", blob_name);
    }
    else
    {
        test.tprintf("%s FAILED\n", blob_name);
    }
}

int check_longblob_data(TestConnections* Test,
                        MYSQL* conn,
                        size_t chunk_size,
                        size_t chunks,
                        int rows)
{
    // char *select_stmt = (char *) "SELECT id, x, b FROM long_blob_table WHERE id = ?";
    char* select_stmt = (char*) "SELECT id, x, b FROM long_blob_table ";
    MYSQL_STMT* stmt = mysql_stmt_init(Test->maxscale->conn_rwsplit);
    if (stmt == NULL)
    {
        Test->add_result(1, "stmt init error: %s\n", mysql_error(Test->maxscale->conn_rwsplit));
    }

    Test->add_result(mysql_stmt_prepare(stmt, select_stmt, strlen(select_stmt)),
                     "Error preparing stmt: %s\n",
                     mysql_stmt_error(stmt));

    MYSQL_BIND param[1], result[3];
    int id = 1;

    memset(param, 0, sizeof(param));
    memset(result, 0, sizeof(result));

    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &id;

    uint8_t* data = (uint8_t*) malloc(chunk_size * chunks);


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

    result[2].buffer_type = MYSQL_TYPE_LONG_BLOB;
    result[2].buffer = data;
    result[2].buffer_length = chunk_size * chunks;

    /*
     *   if (mysql_stmt_bind_param(stmt, param) != 0)
     *   {
     *       printf("Could not bind parameters\n");
     *       return 1;
     *   }
     */
    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        printf("Could not bind results: %s\n", mysql_stmt_error(stmt));
        return 1;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Test->tprintf("Error executing stmt %s\n", mysql_error(Test->maxscale->conn_rwsplit));
    }

    if (mysql_stmt_store_result(stmt) != 0)
    {
        printf("Could not buffer result set: %s\n", mysql_stmt_error(stmt));
        return 1;
    }

    int row = 0;
    while (!mysql_stmt_fetch(stmt))
    {
        Test->tprintf("id=%d\tx=%d\n", r_id, r_x);
        if (r_id != row + 1)
        {
            Test->add_result(1, "id field is wrong! Expected %d, but it is %d\n", row + 1, r_id);
        }

        for (size_t i = 0; i < chunks; i++)
        {
            uint8_t check = 0;

            for (size_t y = 0; y < chunk_size; y++)
            {
                size_t idx = i * chunk_size + y;

                if (data[idx] != check)
                {
                    Test->add_result(1, "byte %lu: expected %hhu, got %hhu", idx, check, data[idx]);
                    break;
                }

                ++check;
            }
        }

        row++;
    }
    if (row != rows)
    {
        Test->add_result(1, "Wrong number of rows in the table! Expected %d, but it is %d\n", rows, row);
    }
    mysql_stmt_free_result(stmt);

    mysql_stmt_close(stmt);

    free(data);

    return 0;
}
