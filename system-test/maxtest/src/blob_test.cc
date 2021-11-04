#include <maxtest/blob_test.hh>
#include <numeric>

int test_longblob(TestConnections* Test,
                  MYSQL* conn,
                  char* blob_name,
                  unsigned long chunk_size,
                  int chunks,
                  int rows)
{
    int size = chunk_size;
    MYSQL_BIND param[1];
    char sql[256];
    int global_res = Test->global_result;

    char* insert_stmt = (char*) "INSERT INTO long_blob_table(x, b) VALUES(1, ?)";

    Test->tprintf("Creating table with %s\n", blob_name);
    Test->try_query(conn, (char*) "DROP TABLE IF EXISTS long_blob_table");
    sprintf(sql,
            "CREATE TABLE long_blob_table(id int NOT NULL AUTO_INCREMENT, x INT, b %s, PRIMARY KEY (id))",
            blob_name);
    Test->try_query(conn, "%s", sql);

    for (int k = 0; k < rows; k++)
    {
        Test->tprintf("Preparintg INSERT stmt\n");
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (stmt == NULL)
        {
            Test->add_result(1, "stmt init error: %s\n", mysql_error(conn));
        }

        Test->add_result(mysql_stmt_prepare(stmt, insert_stmt, strlen(insert_stmt)),
                         "Error preparing stmt: %s\n",
                         mysql_stmt_error(stmt));

        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].is_null = 0;

        Test->tprintf("Binding parameter\n");
        Test->add_result(mysql_stmt_bind_param(stmt, param),
                         "Error parameter binding: %s\n",
                         mysql_stmt_error(stmt));

        Test->tprintf("Filling buffer\n");

        std::vector<uint8_t> data(size, 0);
        std::iota(data.begin(), data.end(), 0);

        Test->tprintf("Sending data in %lu bytes chunks, total size is %lu\n",
                      data.size(), data.size() * chunks);

        for (int i = 0; i < chunks; i++)
        {
            Test->tprintf("Chunk #%d\n", i);

            if (mysql_stmt_send_long_data(stmt, 0, (const char*)data.data(), data.size()) != 0)
            {
                Test->add_result(1, "Error inserting data, iteration %d, error %s\n",
                                 i, mysql_stmt_error(stmt));
                return 1;
            }
        }

        Test->tprintf("Executing statement: %02d\n", k);
        Test->add_result(mysql_stmt_execute(stmt),
                         "INSERT Statement with %s failed, error is %s\n",
                         blob_name, mysql_stmt_error(stmt));
        Test->add_result(mysql_stmt_close(stmt), "Error closing stmt\n");
    }

    if (global_res == Test->global_result)
    {
        Test->tprintf("%s is OK\n", blob_name);
    }
    else
    {
        Test->tprintf("%s FAILED\n", blob_name);
    }

    return 0;
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
