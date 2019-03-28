#pragma once

#include "testconnections.h"

#define FAILOVER_WAIT_TIME 20

char virtual_ip[16];

char* print_version_string(TestConnections* Test);
void  configure_keepalived(TestConnections* Test, char* keepalived_file);
void  stop_keepalived(TestConnections* Test);
