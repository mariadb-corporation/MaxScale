/**
 * MXS-1121: MariaDB 10.2 Bulk Insert test
 *
 * This test is a copy of one of the examples for bulk inserts:
 * https://mariadb.com/kb/en/mariadb/bulk-insert-column-wise-binding/
 */

#include "testconnections.h"

static int show_mysql_error(MYSQL *mysql)
{
    printf("Error(%d) [%s] \"%s\"\n", mysql_errno(mysql),
           mysql_sqlstate(mysql),
           mysql_error(mysql));
    return 1;
}

static int show_stmt_error(MYSQL_STMT *stmt)
{
    printf("Error(%d) [%s] \"%s\"\n", mysql_stmt_errno(stmt),
           mysql_stmt_sqlstate(stmt),
           mysql_stmt_error(stmt));
    return 1;
}

int bind_by_column(MYSQL *mysql)
{
    MYSQL_STMT *stmt;
    MYSQL_BIND bind[3];

    /* Data for insert */
    const char *surnames[] = {"Widenius", "Axmark", "N.N."};
    unsigned long surnames_length[] = {8, 6, 4};
    const char *forenames[] = {"Monty", "David", "will be replaced by default value"};
    char forename_ind[] = {STMT_INDICATOR_NTS, STMT_INDICATOR_NTS, STMT_INDICATOR_DEFAULT};
    char id_ind[] = {STMT_INDICATOR_NULL, STMT_INDICATOR_NULL, STMT_INDICATOR_NULL};
    unsigned int array_size = 3;

    if (mysql_query(mysql, "DROP TABLE IF EXISTS test.bulk_example1"))
    {
        return show_mysql_error(mysql);
    }

    if (mysql_query(mysql, "CREATE TABLE test.bulk_example1 (id INT NOT NULL AUTO_INCREMENT PRIMARY KEY," \
                    "forename CHAR(30) NOT NULL DEFAULT 'unknown', surname CHAR(30))"))
    {
        return show_mysql_error(mysql);
    }

    stmt = mysql_stmt_init(mysql);
    if (mysql_stmt_prepare(stmt, "INSERT INTO test.bulk_example1 VALUES (?,?,?)", -1))
    {
        return show_stmt_error(stmt);
    }

    memset(bind, 0, sizeof(MYSQL_BIND) * 3);

    /* We autogenerate id's, so all indicators are STMT_INDICATOR_NULL */
    bind[0].u.indicator = id_ind;
    bind[0].buffer_type = MYSQL_TYPE_LONG;

    bind[1].buffer = forenames;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].u.indicator = forename_ind;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = surnames;
    bind[2].length = surnames_length;

    /* set array size */
    mysql_stmt_attr_set(stmt, STMT_ATTR_ARRAY_SIZE, &array_size);

    /* bind parameter */
    mysql_stmt_bind_param(stmt, bind);

    /* execute */
    if (mysql_stmt_execute(stmt))
    {
        return show_stmt_error(stmt);
    }

    mysql_stmt_close(stmt);

    /* Check that the rows were inserted */
    if (mysql_query(mysql, "SELECT * FROM test.bulk_example1"))
    {
        return show_mysql_error(mysql);
    }

    MYSQL_RES *res = mysql_store_result(mysql);

    if (res == NULL || mysql_num_rows(res) != 3)
    {
        printf("Expected 3 rows but got %d (%s)\n", res ? (int)mysql_num_rows(res) : 0, mysql_error(mysql));
        return 1;
    }

    if (mysql_query(mysql, "DROP TABLE test.bulk_example1"))
    {
        return show_mysql_error(mysql);
    }

    return 0;
}

int bind_by_row(MYSQL *mysql)
{
    MYSQL_STMT *stmt;
    MYSQL_BIND bind[3];

    struct st_data
    {
        unsigned long id;
        char id_ind;
        char forename[30];
        char forename_ind;
        char surname[30];
        char surname_ind;
    };

    struct st_data data[] =
    {
        {0, STMT_INDICATOR_NULL, "Monty", STMT_INDICATOR_NTS, "Widenius", STMT_INDICATOR_NTS},
        {0, STMT_INDICATOR_NULL, "David", STMT_INDICATOR_NTS, "Axmark", STMT_INDICATOR_NTS},
        {0, STMT_INDICATOR_NULL, "default", STMT_INDICATOR_DEFAULT, "N.N.", STMT_INDICATOR_NTS},
    };

    unsigned int array_size = 3;
    size_t row_size = sizeof(struct st_data);

    if (mysql_query(mysql, "DROP TABLE IF EXISTS bulk_example2"))
    {
        show_mysql_error(mysql);
    }

    if (mysql_query(mysql, "CREATE TABLE bulk_example2 (id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,"\
                    "forename CHAR(30) NOT NULL DEFAULT 'unknown', surname CHAR(30))"))
    {
        show_mysql_error(mysql);
    }

    stmt = mysql_stmt_init(mysql);
    if (mysql_stmt_prepare(stmt, "INSERT INTO bulk_example2 VALUES (?,?,?)", -1))
    {
        show_stmt_error(stmt);
    }

    memset(bind, 0, sizeof(MYSQL_BIND) * 3);

    /* We autogenerate id's, so all indicators are STMT_INDICATOR_NULL */
    bind[0].u.indicator = &data[0].id_ind;
    bind[0].buffer_type = MYSQL_TYPE_LONG;

    bind[1].buffer = &data[0].forename;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].u.indicator = &data[0].forename_ind;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = &data[0].surname;
    bind[2].u.indicator = &data[0].surname_ind;

    /* set array size */
    mysql_stmt_attr_set(stmt, STMT_ATTR_ARRAY_SIZE, &array_size);

    /* set row size */
    mysql_stmt_attr_set(stmt, STMT_ATTR_ROW_SIZE, &row_size);

    /* bind parameter */
    mysql_stmt_bind_param(stmt, bind);

    /* execute */
    if (mysql_stmt_execute(stmt))
    {
        show_stmt_error(stmt);
    }

    mysql_stmt_close(stmt);


    /* Check that the rows were inserted */
    if (mysql_query(mysql, "SELECT * FROM test.bulk_example2"))
    {
        return show_mysql_error(mysql);
    }

    MYSQL_RES *res = mysql_store_result(mysql);

    if (res == NULL || mysql_num_rows(res) != 3)
    {
        printf("Expected 3 rows but got %d (%s)\n", res ? (int)mysql_num_rows(res) : 0, mysql_error(mysql));
        return 1;
    }

    if (mysql_query(mysql, "DROP TABLE test.bulk_example2"))
    {
        return show_mysql_error(mysql);
    }

}

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.2");
    TestConnections test(argc, argv);
    test.connect_maxscale();
    test.repl->connect();

    test.tprintf("Testing column-wise binding with a direct connection");
    test.add_result(bind_by_column(test.repl->nodes[0]), "Bulk inserts with a direct connection should work");
    test.tprintf("Testing column-wise binding with readwritesplit");
    test.add_result(bind_by_column(test.conn_rwsplit), "Bulk inserts with readwritesplit should work");
    test.tprintf("Testing column-wise binding with readconnroute");
    test.add_result(bind_by_column(test.conn_master), "Bulk inserts with readconnroute should work");

    test.tprintf("Testing row-wise binding with a direct connection");
    test.add_result(bind_by_row(test.repl->nodes[0]), "Bulk inserts with a direct connection should work");
    test.tprintf("Testing row-wise binding with readwritesplit");
    test.add_result(bind_by_row(test.conn_rwsplit), "Bulk inserts with readwritesplit should work");
    test.tprintf("Testing row-wise binding with readconnroute");
    test.add_result(bind_by_row(test.conn_master), "Bulk inserts with readconnroute should work");

    test.close_maxscale_connections();
    return test.global_result;
}
