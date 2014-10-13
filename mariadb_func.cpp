#include "mariadb_func.h"


MYSQL * open_conn_db(int port, char * ip, char * db)
{
    MYSQL * conn = mysql_init(NULL);

    if(conn == NULL)
    {
        fprintf(stdout, "Error: can't create MySQL-descriptor\n");
        return(NULL);
    }
    if(!mysql_real_connect(conn,
                           ip,
                           "skysql",
                           "skysql",
                           db,
                           port,
                           NULL,
                           CLIENT_MULTI_STATEMENTS
                           ))
    {
        printf("Error: can't connect to database %s\n", mysql_error(conn));
        return(NULL);
    }

    return(conn);
}


MYSQL * open_conn(int port, char * ip)
{
    return(open_conn_db(port, ip, (char *) "test"));
}

MYSQL * open_conn_no_db(int port, char * ip)
{
    return(open_conn_db(port, ip, NULL));
}

int execute_query(MYSQL *conn, const char *sql)
{
    MYSQL_RES *res;
    if (conn != NULL) {
        if(mysql_query(conn, sql) != 0) {
            printf("Error: can't execute SQL-query: %s\n", sql);
            printf("%s\n\n", mysql_error(conn));
            return(1);
        } else {
            do {
                res = mysql_store_result(conn);
                mysql_free_result(res);
            } while ( mysql_next_result(conn) == 0 );
            return(0);
        }
    } else {
        printf("Connection is broken\n");
        return(1);
    }
}


int get_conn_num(MYSQL *conn, char * ip, char * db)
{
    MYSQL_RES *res;
    MYSQL_ROW row;
    unsigned long long int num_fields;
    //unsigned long long int row_i=0;
    unsigned long long int rows;
    unsigned long long int i;
    unsigned int conn_num = 0;
    if (conn != NULL) {
        if(mysql_query(conn, "show processlist;") != 0) {
            printf("Error: can't execute SQL-query: show processlist\n");
            printf("%s\n\n", mysql_error(conn));
            conn_num = 0;
        } else {
            res = mysql_store_result(conn);
            if(res == NULL) {
                printf("Error: can't get the result description\n");
                conn_num = -1;
            } else {
                num_fields = mysql_num_fields(res);
                rows = mysql_num_rows(res);
                for (i = 0; i < rows; i++) {
                    row = mysql_fetch_row(res);
                    if ( (row[2] != NULL ) && (row[3] != NULL) ) {
                        if ((strstr(row[2], ip) != NULL) && (strstr(row[3], db) != NULL)) {conn_num++;}
                    }
                }
            }
            mysql_free_result(res);
        }
    }
    return(conn_num);
}

int find_status_field(MYSQL *conn, char * sql, char * field_name, char * value)
{
    MYSQL_RES *res;
    MYSQL_ROW row;
    MYSQL_FIELD *field;
    unsigned long long int num_fields;
    unsigned int ret = 1;
    unsigned long long int filed_i = 0;
    unsigned long long int i = 0;

    if (conn != NULL ) {
        if(mysql_query(conn, sql) != 0) {
            printf("Error: can't execute SQL-query: %s\n", sql);
            printf("%s\n\n", mysql_error(conn));
        } else {
            res = mysql_store_result(conn);
            if(res == NULL) {
                printf("Error: can't get the result description\n");
            } else {
                num_fields = mysql_num_fields(res);

                while((field = mysql_fetch_field(res)))
                {
                    if (strstr(field->name, field_name) != NULL) {filed_i = i; ret = 0;}
                    i++;
                }
                if (mysql_num_rows(res) > 0) {
                    row = mysql_fetch_row(res);
                    sprintf(value, "%s", row[filed_i]);
                }
            }
            mysql_free_result(res);
        }
    }
    return(ret);
}

unsigned int get_Seconds_Behind_Master(MYSQL *conn)
{
    char SBM_str[16];
    unsigned int SBM=0;
    if (find_status_field(
                conn, (char *) "show slave status;",
                (char *) "Seconds_Behind_Master", &SBM_str[0]
                ) != 1) {
        sscanf(SBM_str, "%u", &SBM);
    }
    return(SBM);
}
