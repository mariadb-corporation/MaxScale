/**
 * @file mariadb_func.cpp - basic DB interaction routines
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 17/11/14	Timofey Turenko	Initial implementation
 *
 * @endverbatim
 */


#include "mariadb_func.h"

/**
 * Opens connection to DB: wropper over mysql_real_connect
 *
 * @param port	DB server port
 * @param ip	DB server IP address
 * @param db    name of DB to connect
 * @param User  User name
 * @param Password  Password
 * @param flag  Connections flags
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn_db_flags(int port, char * ip, char * db, char * User, char * Password, unsigned long flag)
{
    MYSQL * conn = mysql_init(NULL);

    if(conn == NULL)
    {
        fprintf(stdout, "Error: can't create MySQL-descriptor\n");
        return(NULL);
    }
    if(!mysql_real_connect(conn,
                           ip,
                           User,
                           Password,
                           db,
                           port,
                           NULL,
                           flag
                           ))
    {
        printf("Error: can't connect to database %s\n", mysql_error(conn));
        return(NULL);
    }

    return(conn);
}

/**
 * Opens connection to DB with default flags
 *
 * @param port	DB server port
 * @param ip	DB server IP address
 * @param db    name of DB to connect
 * @param User  User name
 * @param Password  Password
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn_db(int port, char * ip, char * db, char * User, char * Password)
{
    return(open_conn_db_flags(port, ip, db, User, Password, CLIENT_MULTI_STATEMENTS));
}

/**
 * Opens connection to 'test' with default flags
 *
 * @param port	DB server port
 * @param ip	DB server IP address
 * @param User  User name
 * @param Password  Password
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn(int port, char * ip, char * User, char * Password)
{
    return(open_conn_db(port, ip, (char *) "test", User, Password));
}

/**
 * Opens connection to with default flags without defning DB name (just conecto server)
 *
 * @param port	DB server port
 * @param ip	DB server IP address
 * @param User  User name
 * @param Password  Password
 * @return MYSQL struct or NULL in case of error
 */
MYSQL * open_conn_no_db(int port, char * ip, char *User, char *Password)
{
    return(open_conn_db_flags(port, ip, NULL, User, Password, CLIENT_MULTI_STATEMENTS));
}

/**
 * Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clea up returns
 *
 * @param MYSQL	connection struct
 * @param sql	SQL string
 * @return 0 in case of success
 */
int execute_query(MYSQL *conn, const char *sql)
{
        return(execute_query1(conn, sql, false));
}

/**
 * Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clea up returns; function do not produce any printing
 *
 * @param MYSQL	connection struct
 * @param sql	SQL string
 * @return 0 in case of success
 */
int execute_query_silent(MYSQL *conn, const char *sql)
{
    return(execute_query1(conn, sql, true));
}

/**
 * Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clea up returns; function do not produce any printing
 *
 * @param MYSQL	connection struct
 * @param sql	SQL string
 * @param silent if true function do not produce any printing
 * @return 0 in case of success
 */
int execute_query1(MYSQL *conn, const char *sql, bool silent)
{
    MYSQL_RES *res;
    if (conn != NULL) {
        if(mysql_query(conn, sql) != 0) {
            if (!silent) {
                printf("Error: can't execute SQL-query: %s\n", sql);
                printf("%s\n\n", mysql_error(conn));
            }
            return(1);
        } else {
            do {
                res = mysql_store_result(conn);
                mysql_free_result(res);
            } while ( mysql_next_result(conn) == 0 );
            return(0);
        }
    } else {
        if (!silent) {printf("Connection is broken\n");}
        return(1);
    }
}

int execute_query_check_one(MYSQL *conn, const char *sql, const char *expected)
{
    MYSQL_RES *res;
    MYSQL_ROW row;
    int r = 0;
    if (conn != NULL) {
        if(mysql_query(conn, sql) != 0) {
            printf("Error: can't execute SQL-query: %s\n", sql);
            printf("%s\n\n", mysql_error(conn));
            return(1);
        } else {
            res = mysql_store_result(conn);
            if (mysql_num_rows(res) == 1) {
                row = mysql_fetch_row(res);
                if (row[0] != NULL) {
                    r = strcmp(row[0], expected);
                    if (r != 0) {
                        printf("First field is '%s'\n", row[0]);
                    }
                } else {
                    r = 1;
                    printf("First field is NULL\n");
                }
            }
            else {
                r = 1;
                printf("Number of rows is %llu\n", mysql_num_rows(res));
            }

            mysql_free_result(res);

            do {
                res = mysql_store_result(conn);
                mysql_free_result(res);
            } while ( mysql_next_result(conn) == 0 );
            return(r);
        }
    } else {
        printf("Connection is broken\n");
        return(1);
    }
}



/**
 * Executes SQL query and store 'affected rows' number in affectet_rows parameter
 *
 * @param MYSQL	connection struct
 * @param sql	SQL string
 * @param affected_rows pointer to variabe to store number of affected rows
 * @return 0 in case of success
 */
int execute_query_affected_rows(MYSQL *conn, const char *sql, my_ulonglong * affected_rows)
{
    MYSQL_RES *res;
    if (conn != NULL) {
        if(mysql_query(conn, sql) != 0) {
            printf("Error: can't execute SQL-query: %s\n", sql);
            printf("%s\n\n", mysql_error(conn));
            return(1);
        } else {
            do {
                *affected_rows = mysql_affected_rows(conn);
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

/**
 * Executes 'show processlist' and calculates number of connections from definec host to defined DB
 *
 * @param MYSQL	connection struct
 * @param ip	connections from this IP address are counted
 * @param db    name of DB to which connections are counted
 * @return number of connections
 */
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

/**
 * Find given filed in the SQL query reply
 *
 * @param MYSQL	connection struct
 * @param sql	SQL query to execute
 * @param filed_name    name of field to find
 * @param value pointer to variable to store value of found field
 * @return 0 in case of success
 */
int find_field(MYSQL *conn, const char *sql, const char *field_name, char * value)
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
            do {
                res = mysql_store_result(conn);
                mysql_free_result(res);
            } while ( mysql_next_result(conn) == 0 );
        }
    }
    return(ret);
}

/**
 * Return the value of SECONDS_BEHIND_MASTER
 *
 * @param MYSQL	connection struct
 * @return value of SECONDS_BEHIND_MASTER
 */
unsigned int get_seconds_behind_master(MYSQL *conn)
{
    char SBM_str[16];
    unsigned int SBM=0;
    if (find_field(
                conn, (char *) "show slave status;",
                (char *) "Seconds_Behind_Master", &SBM_str[0]
                ) != 1) {
        sscanf(SBM_str, "%u", &SBM);
    }
    return(SBM);
}


/**
 * Reads MaxScale log file
 *
 * @param name  Name of log file (full path)
 * @param err_log_content   pointer to the buffer to store log file content
 * @return 0 in case of success, 1 in case of error
 */
int read_log(char * name, char ** err_log_content)
{
    FILE *f;

    f = fopen(name,"rb");
    if (f != NULL) {

        int prev=ftell(f);
        fseek(f, 0L, SEEK_END);
        long int size=ftell(f);
        fseek(f, prev, SEEK_SET);
        *err_log_content = (char *)malloc(size+2);
        if (*err_log_content != NULL) {
            fread(*err_log_content, 1, size, f);
            //err_log_content[size]=0;
            return(0);
        } else {
            printf("Error allocationg memory for the log\n");
            return(1);
        }
    }
    else {
        printf ("Error reading log %s \n", name);
        return(1);
    }
}
