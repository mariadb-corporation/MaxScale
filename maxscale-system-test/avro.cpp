/**
 * @file avro.cpp test of avro
 * - setup binlog and avro
 * - put some data to t1
 * - check avro file with "maxavrocheck -vv /var/lib/maxscale/avro/test.t1.000001.avro"
 * - check that data in avro file is correct
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"
#include <jansson.h>
#include "maxinfo_func.h"

#include <sstream>
#include <iostream>

using std::cout;
using std::endl;

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(600);
    Test->stop_maxscale();
    Test->ssh_maxscale(true, (char *) "rm -rf /var/lib/maxscale/avro");

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], "DROP TABLE IF EXISTS t1");
    Test->repl->close_connections();
    sleep(5);


    Test->start_binlog();

    Test->set_timeout(120);

    Test->stop_maxscale();

    Test->ssh_maxscale(true, "rm -rf /var/lib/maxscale/avro");

    Test->set_timeout(120);

    Test->start_maxscale();

    Test->set_timeout(60);

    Test->repl->connect();
    create_t1(Test->repl->nodes[0]);
    insert_into_t1(Test->repl->nodes[0], 3);
    execute_query(Test->repl->nodes[0], "FLUSH LOGS");

    Test->repl->close_connections();

    Test->set_timeout(120);

    sleep(10);

    char * avro_check = Test->ssh_maxscale_output(true,
                        "maxavrocheck -vv /var/lib/maxscale/avro/test.t1.000001.avro | grep \"{\"");
    char * output = Test->ssh_maxscale_output(true, "maxavrocheck -d /var/lib/maxscale/avro/test.t1.000001.avro");

    std::istringstream iss;
    iss.str(output);
    int x1_exp = 0;
    int fl_exp = 0;
    int x = 16;

    for (std::string line; std::getline(iss, line);)
    {
        long long int x1, fl;
        Test->set_timeout(20);
        get_x_fl_from_json((char*)line.c_str(), &x1, &fl);

        if (x1 != x1_exp || fl != fl_exp)
        {
            Test->add_result(1, "Output:x1 %lld, fl %lld, Expected: x1 %d, fl %d",
                             x1, fl, x1_exp, fl_exp);
            break;
        }

        if ((++x1_exp) >= x)
        {
            x1_exp = 0;
            x = x * 16;
            fl_exp++;
            Test->tprintf("fl = %d", fl_exp);
        }
    }

    if (fl_exp != 3)
    {
        Test->add_result(1, "not enough lines in avrocheck output\n");
    }

    Test->set_timeout(120);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

