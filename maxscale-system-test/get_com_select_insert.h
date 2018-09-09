#ifndef GET_COM_SELECT_INSERT_H
#define GET_COM_SELECT_INSERT_H

#include "testconnections.h"

/**
 * @brief get_global_status_allnodes Reads COM_SELECT and COM_INSERT variables from all nodes and stores into
 *'selects' and 'inserts'
 * @param selects pointer to array to store COM_SELECT for all nodes
 * @param inserts pointer to array to store COM_INSERT for all nodes
 * @param nodes Mariadb_nodes object that contains information about nodes
 * @param silent if 1 do not print anything
 * @return 0 in case of success
 */
int get_global_status_allnodes(long int* selects, long int* inserts, Mariadb_nodes* nodes, int silent);

/**
 * @brief print_delta Prints difference in COM_SELECT and COM_INSERT
 * @param new_selects pointer to array to store COM_SELECT for all nodes after test
 * @param new_inserts pointer to array to store COM_INSERT for all nodes after test
 * @param selects pointer to array to store COM_SELECT for all nodes before test
 * @param inserts pointer to array to store COM_INSERT for all nodes before test
 * @param NodesNum Number of nodes
 * @return
 */
int print_delta(long int* new_selects,
                long int* new_inserts,
                long int* selects,
                long int* inserts,
                int nodes_num);


#endif // GET_COM_SELECT_INSERT_H
