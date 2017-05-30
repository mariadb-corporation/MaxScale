#ifndef DIFFERENT_SIZE_H
#define DIFFERENT_SIZE_H

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

char * create_event_size(unsigned long size);
MYSQL * connect_to_serv(TestConnections* Test, bool binlog);
void set_max_packet(TestConnections* Test, bool binlog, char * cmd);
void different_packet_size(TestConnections* Test, bool binlog);

#endif // DIFFERENT_SIZE_H
