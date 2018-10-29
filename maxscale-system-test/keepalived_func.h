#ifndef KEEPALIVED_FUNC_H
#define KEEPALIVED_FUNC_H

#include "testconnections.h"

#define FAILOVER_WAIT_TIME 20

char virtual_ip[16];
char* print_version_string(TestConnections* Test);
void  configure_keepalived(TestConnections* Test, char* keepalived_file);
void  stop_keepalived(TestConnections* Test);

#endif      // KEEPALIVED_FUNC_H
