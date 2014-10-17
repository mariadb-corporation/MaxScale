#ifndef MARIADB_FUNC_H
#define MARIADB_FUNC_H

#include <my_config.h>
#include <my_global.h>
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

MYSQL * open_conn(int port, char * ip, char *User, char *Password);
MYSQL * open_conn_no_db(int port, char * ip, char *User, char *Password);
int execute_query(MYSQL *conn, const char *sql);
int get_conn_num(MYSQL *conn, char * ip, char * db);
int find_status_field(MYSQL *conn, char * sql, char * field_name, char * value);
unsigned int get_Seconds_Behind_Master(MYSQL *conn);
int ReadLog(char * name, char ** err_log_content);

#endif // MARIADB_FUNC_H
