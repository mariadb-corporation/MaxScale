#include "blob_test.h"

int test_longblob(TestConnections* Test, MYSQL * conn, char * blob_name, unsigned long chunk_size, int chunks,
                  int rows)
{
    int size = chunk_size;
    unsigned long * data;
    int i, j;
    MYSQL_BIND param[1];
    char sql[256];
    int global_res = Test->global_result;
    //Test->tprintf("chunk size %lu chunks %d inserts %d\n", chunk_size, chunks, rows);

    char *insert_stmt = (char *) "INSERT INTO long_blob_table(x, b) VALUES(1, ?)";

    Test->tprintf("Creating table with %s\n", blob_name);
    Test->try_query(conn, (char *) "DROP TABLE IF EXISTS long_blob_table");
    sprintf(sql, "CREATE TABLE long_blob_table(id int NOT NULL AUTO_INCREMENT, x INT, b %s, PRIMARY KEY (id))",
            blob_name);
    Test->try_query(conn, sql);

    for (int k = 0; k < rows; k++)
    {
        Test->tprintf("Preparintg INSERT stmt\n");
        MYSQL_STMT * stmt = mysql_stmt_init(conn);
        if (stmt == NULL)
        {
            Test->add_result(1, "stmt init error: %s\n", mysql_error(conn));
        }

        Test->add_result(mysql_stmt_prepare(stmt, insert_stmt, strlen(insert_stmt)), "Error preparing stmt: %s\n",
                         mysql_stmt_error(stmt));

        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].is_null = 0;

        Test->tprintf("Binding parameter\n");
        Test->add_result(mysql_stmt_bind_param(stmt, param), "Error parameter binding: %s\n", mysql_stmt_error(stmt));

        Test->tprintf("Filling buffer\n");
        data = (unsigned long *) malloc(size * sizeof(long int));

        if (data == NULL)
        {
            Test->add_result(1, "Memory allocation error\n");
        }



        Test->tprintf("Sending data in %d bytes chunks, total size is %d\n", size * sizeof(unsigned long),
                      (size * sizeof(unsigned long)) * chunks);
        for (i = 0; i < chunks; i++)
        {
            for (j = 0; j < size; j++)
            {
                data[j] = j + i * size;
            }
            Test->set_timeout(300);
            Test->tprintf("Chunk #%d\n", i);
            if (mysql_stmt_send_long_data(stmt, 0, (char *) data, size * sizeof(unsigned long)) != 0)
            {
                Test->add_result(1, "Error inserting data, iteration %d, error %s\n", i, mysql_stmt_error(stmt));
                return 1;
            }
        }

        //for (int k = 0; k < rows; k++)
        //{
        Test->tprintf("Executing statement: %02d\n", k);
        Test->set_timeout(3000);
        Test->add_result(mysql_stmt_execute(stmt), "INSERT Statement with %s failed, error is %s\n", blob_name,
                         mysql_stmt_error(stmt));
        //}
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

int check_longblob_data(TestConnections* Test, MYSQL * conn, unsigned long chunk_size, int chunks,
                        int rows)
{
    //char *select_stmt = (char *) "SELECT id, x, b FROM long_blob_table WHERE id = ?";
    char *select_stmt = (char *) "SELECT id, x, b FROM long_blob_table ";
    MYSQL_STMT * stmt = mysql_stmt_init(Test->maxscales->conn_rwsplit[0]);
    if (stmt == NULL)
    {
        Test->add_result(1, "stmt init error: %s\n", mysql_error(Test->maxscales->conn_rwsplit[0]));
    }

    Test->add_result(mysql_stmt_prepare(stmt, select_stmt, strlen(select_stmt)), "Error preparing stmt: %s\n",
                     mysql_stmt_error(stmt));

    MYSQL_BIND param[1], result[3];
    int id = 1;

    memset(param, 0, sizeof(param));
    memset(result, 0, sizeof(result));

    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &id;

    unsigned long * data = (unsigned long *) malloc(chunk_size * chunks * sizeof(long int));


    int r_id;
    int r_x;
    unsigned long l_id;
    unsigned long l_x;
    my_bool b_id;
    my_bool b_x;
    my_bool e_id;
    my_bool e_x;

    result[0].buffer_type     = MYSQL_TYPE_LONG;
    result[0].buffer         = &r_id;
    result[0].buffer_length = 0;
    result[0].length = &l_id;
    result[0].is_null = &b_id;
    result[0].error = &e_id;

    result[1].buffer_type     = MYSQL_TYPE_LONG;
    result[1].buffer         = &r_x;
    result[1].buffer_length = 0;
    result[1].length = &l_x;
    result[1].is_null = &b_x;
    result[1].error = &e_x;

    result[2].buffer_type     = MYSQL_TYPE_LONG_BLOB;
    result[2].buffer         = data;
    result[2].buffer_length = chunk_size * chunks * sizeof(long int);

    /*
        if (mysql_stmt_bind_param(stmt, param) != 0)
        {
            printf("Could not bind parameters\n");
            return 1;
        }
    */
    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        printf("Could not bind results: %s\n", mysql_stmt_error(stmt));
        return 1;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Test->tprintf("Error executing stmt %s\n", mysql_error(Test->maxscales->conn_rwsplit[0]));
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
        for (int y = 0; y < (int)chunk_size * chunks; y++)
        {
            if ((int)data[y] != y)
            {
                Test->add_result(1, "expected %d, got %d", data[y], y);
                break;
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

    return 0;
}


