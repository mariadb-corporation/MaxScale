/**
 * @file fwf
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    char str[4096];
    char sql[4096];
    char pass_file[4096];
    char deny_file[4096];
    FILE* file;

    Test->read_env();
    Test->print_env();

    int N = 4;

    for (int i = 1; i < N+1; i++){

        sprintf(str, "scp -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s/fw/rules%d root@%s:/home/ec2-user/rules.txt", Test->maxscale_sshkey, Test->test_dir, i, Test->maxscale_IP);
        printf("Copying rules to Maxscale machine: %s\n", str);
        system(str);
        Test->restart_maxscale();
        Test->connect_rwsplit();

        sprintf(pass_file, "%s/fw/pass%d", Test->test_dir, i);
        sprintf(deny_file, "%s/fw/deny%d", Test->test_dir, i);

        file = fopen(pass_file, "r");
        while (fgets(sql, sizeof(sql), file)) {
            printf("%s\n", sql);
            global_result += execute_query(Test->conn_rwsplit, sql);
        }
        fclose(file);

        file = fopen(deny_file, "r");
        while (fgets(sql, sizeof(sql), file)) {
            printf("%s\n", sql);
            execute_query(Test->conn_rwsplit, sql);
            if (mysql_errno(Test->conn_rwsplit) != 1141) {
                printf("Query succeded, but fail expected, errono is %d\n", mysql_errno(Test->conn_rwsplit));
                global_result++;
            }
        }
        fclose(file);

        mysql_close(Test->conn_rwsplit);
    }

    Test->copy_all_logs(); return(global_result);
}


