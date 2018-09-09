/**
 * @file bug479.cpp regression case for bug 479 ( Undefined filter reference in MaxScale.cnf causes a crash)
 *
 * - Maxscale.cnf with "filters=non existing filter | не существуюший фильтер", cheks error log for warnings
 *and
 * - check if Maxscale is alive
 */


/*
 *  Markus Mäkelä 2014-08-15 17:38:06 UTC
 *  Undefined filters in services cause a crash when the service is accessed.
 *
 *  How to reproduce:
 *  Define a service with a filter not defined in the MaxScale.cnf file, start MaxScale and access the
 * service.
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->check_log_err(0, (char*) "Unable to find filter 'non existing filter", true);
    // global_result =Test->check_log_err(0, (char *) "не существуюший фильтер", true);
    // global_result +=Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
