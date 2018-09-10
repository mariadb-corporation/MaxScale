#ifndef FW_COPY_RULES_H
#define FW_COPY_RULES_H

#include "testconnections.h"

/**
 * @brief copy_rules Copy rules file for firewall filter to Maxscale machine
 * @param Test TestConnections object
 * @param rules_name Name of file to be copied
 * @param rules_dir Directory where file is located
 */
void copy_rules(TestConnections* Test, const char* rules_name, const char* rules_dir);

#endif      // FW_COPY_RULES_H
