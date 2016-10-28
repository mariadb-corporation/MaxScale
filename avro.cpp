/**
 * @file avro.cpp test of avro
 * - setup binlog and avro
 * - put some data to t1
 * - check avro file with "maxavrocheck -vv /var/lib/maxscale/avro/test.t1.000001.avro"
 * - check that data in avro file is correct
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"
#include <jansson.h>
#include "maxinfo_func.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(600);
    Test->stop_maxscale();
    Test->ssh_maxscale(TRUE, (char *) "rm -rf /var/lib/maxscale/avro");

    //Test->ssh_maxscale(TRUE, (char *) "mkdir /var/lib/maxscale/avro; chown -R maxscale:maxscale /var/lib/maxscale/avro");

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);


    Test->binlog_cmd_option = 0;
    Test->start_binlog();

    Test->set_timeout(120);

    Test->stop_maxscale();

    Test->ssh_maxscale(TRUE, (char *) "rm -rf /var/lib/maxscale/avro");

    Test->set_timeout(120);

    Test->start_maxscale();

    Test->set_timeout(60);

    Test->repl->connect();
    create_t1(Test->repl->nodes[0]);
    insert_into_t1(Test->repl->nodes[0], 3);
    Test->repl->close_connections();

    Test->set_timeout(120);

    sleep(10);

    char * avro_check = Test->ssh_maxscale_output(TRUE, " maxavrocheck -vv /var/lib/maxscale/avro/test.t1.000001.avro | grep \"{\"");

    //printf("%s\n", avro_check);

    Test->set_timeout(20);

    char * str ;
    char * str_end;
    char line[1024];
    long long int x1;
    long long int fl;
    int x1_exp = 0;
    int fl_exp = 0;
    int x = 16;

    str = avro_check;

    str_end = strstr(str, "\n");
    Test->tprintf("fl = %d\n", fl_exp);
    while (str_end != NULL )
    {
        memcpy(line, str, str_end - str);
        line[str_end - str] = '\0';
        //Test->tprintf("%s\n", line);
        get_x_fl_from_json(line, &x1, &fl);
        if ((x1 != x1_exp) || (fl != fl_exp))
        {
            Test->add_result(1, "Wrong data in avro file: x1 = %lld, fl = %lld, but expected x1 = %d, fl = %d", x1, fl, x1_exp, fl_exp);
        }
        x1_exp++;
        if (x1_exp >= x)
        {
            x1_exp = 0;
            x = x * 16;
            fl_exp++;
            Test->tprintf("fl = %d\n", fl_exp);
        }
        str = str_end + 1 ;
        str_end = strstr(str, "\n");
    }

    if (fl_exp != 3)
    {
        Test->add_result(1, "not enough lines in avrocheck output\n");
    }

    Test->set_timeout(120);

    Test->copy_all_logs(); return(Test->global_result);
}

