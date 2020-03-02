#include "sql_t1.h"

#include <pthread.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static char** sql = NULL;
static size_t sql_size = 0;

int execute_select_query_and_check(MYSQL* conn, const char* sql, unsigned long long int rows)
{
    MYSQL_RES* res;
    MYSQL_ROW row;
    unsigned long long int i;
    unsigned long long int num_fields;
    unsigned long long int int_res;
    unsigned long long int row_i = 0;
    int test_result = 0;
    unsigned long long int rows_from_select = 0;
    int wait_i = 0;

    printf("Trying SELECT, num_of_rows=%llu\n", rows);
    int res_alloc = 0;
    if (conn != NULL)
    {
        rows_from_select = 0;
        wait_i = 0;
        while ((rows_from_select != rows) && (wait_i < 100))
        {
            if (mysql_query(conn, sql) != 0)
            {
                printf("Error: can't execute SQL-query: %s\n", mysql_error(conn));
            }

            res = mysql_store_result(conn);
            res_alloc = 1;
            if (res == NULL)
            {
                printf("Error: can't get the result description\n");
                test_result = 1;
                mysql_free_result(res);
                res_alloc = 0;
                wait_i++;
                sleep(1);
            }
            else
            {
                rows_from_select = mysql_num_rows(res);
                printf("rows=%llu\n", rows_from_select);
                wait_i++;
                if (rows_from_select != rows)
                {
                    printf("Waiting 1 second and trying again...\n");
                    mysql_free_result(res);
                    res_alloc = 0;
                    sleep(1);
                }
            }
        }

        if (rows_from_select != rows)
        {
            printf("SELECT returned %llu rows instead of %llu!\n", rows_from_select, rows);
            test_result = 1;
            printf("sql was %s\n", sql);
        }
        else
        {
            num_fields = mysql_num_fields(res);
            if (num_fields != 2)
            {
                printf("SELECT returned %llu fileds instead of 2!\n", num_fields);
                test_result = 1;
            }
            if (mysql_num_rows(res) > 0)
            {
                while ((row = mysql_fetch_row(res)) != NULL)
                {
                    for (i = 0; i < num_fields; i++)
                    {
                        sscanf(row[i], "%llu", &int_res);
                        if ((i == 0 ) && (int_res != row_i))
                        {
                            printf("SELECT returned wrong result! %llu instead of expected %llu\n",
                                   int_res,
                                   row_i);
                            test_result = 1;
                            printf("sql was %s\n", sql);
                        }
                    }
                    row_i++;
                }
            }
        }
        if (res_alloc != 0)
        {
            mysql_free_result(res);
        }
    }
    else
    {
        printf("FAILED: broken connection\n");
        test_result = 1;
    }

    return test_result;
}
int create_t1(MYSQL* conn)
{
    int result = 0;
    result += execute_query(conn, "DROP TABLE IF EXISTS t1;");
    printf("Creating test table\n");
    result += execute_query(conn, "CREATE TABLE t1 (x1 int, fl int);");
    return result;
}

int create_t2(MYSQL* conn)
{
    int result = 0;
    result += execute_query(conn, "DROP TABLE IF EXISTS t2;");
    printf("Creating test table\n");
    result += execute_query(conn, "CREATE TABLE t2 (x1 int, fl int);");
    return result;
}

static const char ins1[] = "INSERT INTO t1 (x1, fl) VALUES ";

int create_insert_string(char* sql, int N, int fl)
{
    char* wptr = sql;

    strcpy(wptr, ins1);
    for (int i = 0; i < N; i++)
    {
        wptr = strchr(wptr, '\0');
        sprintf(wptr, "(%d, %d),", i, fl);
    }
    wptr = strrchr(wptr, ',');
    sprintf(wptr, ";");
    return 0;
}

char* allocate_insert_string(int fl, int N)
{
    char* rval = NULL;

    pthread_mutex_lock(&mutex);

    if (sql == NULL)
    {
        sql = (char**)calloc(16, sizeof(char*));
        sql_size = 16;
    }

    if ((size_t)fl >= sql_size)
    {
        fprintf(stderr, "Insert index %d is too large, setting it to %lu", fl, sql_size - 1);
        fl = sql_size - 1;
    }

    if (sql[fl] == NULL)
    {
        char tmpstr[256];
        sprintf(tmpstr, "(%d, %d),", N, fl);
        sql[fl] = (char*)malloc(sizeof(ins1) + N * strlen(tmpstr) + 60);
        create_insert_string(sql[fl], N, fl);
    }

    rval = sql[fl];
    pthread_mutex_unlock(&mutex);

    return rval;
}

int insert_into_t1(MYSQL* conn, int N)
{

    int x = 16;
    int result = 0;

    printf("Generating long INSERTs\n");
    for (int i = 0; i < N; i++)
    {
        printf("sql %d, rows=%d\n", i, x);
        char* sqlstr = allocate_insert_string(i, x);
        printf("INSERT: rwsplitter\n");
        printf("Trying INSERT, len=%d\n", x);
        fflush(stdout);
        result += execute_query(conn, "%s", sqlstr);
        fflush(stdout);
        x *= 16;
    }
    return result;
}

int select_from_t1(MYSQL* conn, int N)
{
    int x = 16;
    int result = 0;
    int i;
    char sq[100];

    for (i = 0; i < N; i++)
    {
        sprintf(&sq[0], "select * from t1 where fl=%d;", i);
        result += execute_select_query_and_check(conn, sq, x);
        x = x * 16;
    }
    return result;
}

// 0 - if it does not exist
// -1 - in case of error
int check_if_t1_exists(MYSQL* conn)
{
    MYSQL_RES* res;
    MYSQL_ROW row;

    int t1 = 0;
    if (conn != NULL)
    {
        if (mysql_query(conn, "show tables;") != 0)
        {
            printf("Error: can't execute SQL-query: %s\n", mysql_error(conn));
            t1 = 0;
        }
        else
        {
            res = mysql_store_result(conn);
            if (res == NULL)
            {
                printf("Error: can't get the result description\n");
                t1 = -1;
            }
            else
            {
                mysql_num_fields(res);
                if (mysql_num_rows(res) > 0)
                {
                    while ((row = mysql_fetch_row(res)) != NULL)
                    {
                        if ((row[0] != NULL ) && (strcmp(row[0], "t1") == 0 ))
                        {
                            t1 = 1;
                        }
                    }
                }
            }
            mysql_free_result(res);
        }
    }
    else
    {
        printf("FAILED: broken connection\n");
        t1 = -1;
    }
    return t1;
}
