/**
 * MXS-1804: request 16M-1 stmt_prepare command packet connect hang
 *
 * https://jira.mariadb.org/browse/MXS-1804
 */

#include "testconnections.h"

int sql_str_size(int sqlsize)
{
    char prefx[] = "select ''";
    return sqlsize - strlen(prefx) - 1;
}

void gen_select_sqlstr(char *sqlstr, unsigned int strsize, int sqlsize)
{
    strcpy(sqlstr, "select '");
    memset(sqlstr + strlen("select '"), 'f', strsize);
    sqlstr[sqlsize - 2] = '\'';
    sqlstr[sqlsize - 1] = '\0';
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    int sqlsize = 16777215;
    int strsize = sql_str_size(sqlsize);

    char* sqlstr = (char *)malloc(sqlsize);
    gen_select_sqlstr(sqlstr, strsize, sqlsize);

    test.set_timeout(30);
    test.maxscales->connect();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);
    test.expect(mysql_stmt_prepare(stmt, sqlstr, strlen(sqlstr)) != 0,
		"Prepare should fail in 2.2 but not hang. Error: %s",
                mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);

    return test.global_result;
}
