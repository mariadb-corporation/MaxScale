#ifndef DIFFERENT_SIZE_H
#define DIFFERENT_SIZE_H

#include <iostream>
#include <unistd.h>
#include "testconnections.h"

/**
 * @brief create_event_size Creates SQL query to generate even of given size
 * @param size desired size of event
 * @return SQL query string
 */
char* create_event_size(unsigned long size);

/**
 * @brief connect_to_serv Open connection
 * @param Test TestConnections object
 * @param binlog if true - connects to Master, otherwise - to RWSplit router
 * @return MYSQL handler
 */
MYSQL* connect_to_serv(TestConnections* Test, bool binlog);

/**
 * @brief set_max_packet Executes 'cmd' on Master of RWSplit ('cmd' should be 'set global
 *max_paxket_size=...')
 * @param Test TestConnections object
 * @param binlog if true - connects to Master, otherwise - to RWSplit router
 * @param cmd command to execute
 */
void set_max_packet(TestConnections* Test, bool binlog, char* cmd);

/**
 * @brief different_packet_size Tries INSERTs with size close to 0x0ffffff * N (N is 1, 2 and 3)
 * @param Test TestConnections object
 * @param binlog if true - connects to Master, otherwise - to RWSplit router
 */
void different_packet_size(TestConnections* Test, bool binlog);

#endif // DIFFERENT_SIZE_H
