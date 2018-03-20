/**
 * @file keepalived:masterdown.cpp
 */


#include <iostream>
#include "testconnections.h"
#include "keepalived_func.h"

bool check_maxscale_passive(TestConnections* Test, int node)
{
    bool passive;
    char * passive_str;
    int ec;
    Test->tprintf("Checking status of Maxscale %03d", node);
    passive_str = Test->maxscales->ssh_node_output(node, "maxctrl show maxscale | grep passive", false, &ec);
    Test->tprintf("maxctrl output string: %s\n", passive_str);
    if (strstr(passive_str, "false") != NULL)
    {
        passive = false;
    }
    else
    {
        if (strstr(passive_str, "true") == NULL)
        {
            Test->tprintf("Can't find 'true' or 'false' in the 'maxctrl' output\n");
        }
        passive = true;
    }
    free(passive_str);

    Test->tprintf("Content of 'state.txt' file: %s\n", Test->maxscales->ssh_node_output(0, "cat /tmp/state.txt", false, &ec));
    return passive;
}

int main(int argc, char *argv[])
{
    bool passive;
    char str[1024];

    TestConnections * Test = new TestConnections(argc, argv);
    //Test->set_timeout(10);




    Test->tprintf("Maxscale_N %d\n", Test->maxscales->N);
    if (Test->maxscales->N < 2)
    {
        Test->tprintf("At least 2 Maxscales are needed for this test. Exiting\n");
        exit(0);
    }

    Test->tprintf("Starting replication with GTID\n");
    Test->repl->require_gtid(true);
    Test->repl->start_replication();

    Test->tprintf("Configuring 'keepalived'\n");
    // Get test client IP, replace last number in it with 253 and use it as Virtual IP
    configure_keepalived(Test, (char *) "masterdown");

    //Test->maxscales->ssh_node(1, (char *) "maxctrl alter maxscale passive true", false);

    print_version_string(Test);

    sleep(FAILOVER_WAIT_TIME);sleep(FAILOVER_WAIT_TIME);

    // initial state: 000 expected to be active, 001 - passive
    passive = check_maxscale_passive(Test, 0);
    if (passive)
    {
        Test->add_result(1, "Maxscale 000 is in the passive mode\n");
    }
    passive = check_maxscale_passive(Test, 1);
    if (!passive)
    {
        Test->add_result(1, "Maxscale 001 is NOT in the passive mode\n");
    }

    int first_master = Test->repl->find_master();

    Test->tprintf("Stop Master - node %d\n", first_master);

    Test->repl->stop_node(first_master);

    sleep(FAILOVER_WAIT_TIME);

    int second_master= Test->repl->find_master();

    Test->tprintf("new master is node %d\n", second_master);

    if (first_master == second_master)
    {
        Test->add_result(1, "Failover did not happen\n");
    }

    sprintf(str, "Performing automatic failover to replace failed master 'server%d'", first_master + 1);
    Test->tprintf("Checking Maxscale log on 000 for the failover message %s\n", str);
    Test->check_log_err(0, str , true);
    sprintf(str, "Performing automatic failover to replace failed master");
    Test->tprintf("Checking Maxscale log on 001 for the lack of failover message\n");
    Test->check_log_err(1, str , false);

    passive = check_maxscale_passive(Test, 0);
    if (passive)
    {
        Test->add_result(1, "Maxscale 000 is in the passive mode\n");
    }
    passive = check_maxscale_passive(Test, 1);
    if (!passive)
    {
        Test->add_result(1, "Maxscale 001 is NOT in the passive mode\n");
    }

    Test->tprintf("Stop Maxscale 000\n");

    Test->maxscales->stop_maxscale(0);

    sleep(FAILOVER_WAIT_TIME);

    passive = check_maxscale_passive(Test, 1);
    if (passive)
    {
        Test->add_result(1, "Maxscale 001 is in the passive mode\n");
    }

    Test->tprintf("Stop new Master - node %d\n", second_master);
    Test->repl->stop_node(second_master);
    sleep(FAILOVER_WAIT_TIME);

    int third_master= Test->repl->find_master();

    Test->tprintf("new master (third one) is node %d\n", third_master);

    if (second_master == third_master)
    {
        Test->add_result(1, "Failover did not happen\n");
    }
    sprintf(str, "Performing automatic failover to replace failed master 'server%d'", second_master + 1);
    Test->tprintf("Checking Maxscale log on 001 for the failover message %s\n", str);
    Test->check_log_err(1, str , true);

    Test->check_log_err(1, (char *) "Multiple failed master servers detected" , false);
    Test->check_log_err(1, (char *) "Failed to perform failover" , false);
    Test->check_log_err(1, (char *) "disabling automatic failover" , false);

    Test->tprintf("Start Maxscale 000\n");

    Test->maxscales->start_maxscale(0);

    sleep(FAILOVER_WAIT_TIME);

    passive = check_maxscale_passive(Test, 0);
    if (passive)
    {
        Test->add_result(1, "Maxscale 000 is in the passive mode\n");
    }
    passive = check_maxscale_passive(Test, 1);
    if (!passive)
    {
        Test->add_result(1, "Maxscale 001 is NOT in the passive mode\n");
    }

    sprintf(str, "Performing automatic failover to replace failed master 'server%d'", second_master + 1);
    Test->tprintf("Checking Maxscale log on 001 for the failover message %s\n", str);
    Test->check_log_err(1, str , true);
    Test->tprintf("Checking Maxscale log on 000 for the lack of failover message %s\n", str);
    Test->check_log_err(0, str , false);

    Test->check_log_err(1, (char *) "Multiple failed master servers detected" , false);
    Test->check_log_err(1, (char *) "Failed to perform failover" , false);
    Test->check_log_err(1, (char *) "disabling automatic failover" , false);

    Test->check_log_err(0, (char *) "Multiple failed master servers detected" , false);
    Test->check_log_err(0, (char *) "Failed to perform failover" , false);
    Test->check_log_err(0, (char *) "disabling automatic failover" , false);


//    Test->repl->require_gtid(false);
//    Test->repl->start_replication();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
