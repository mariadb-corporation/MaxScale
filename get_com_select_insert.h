#ifndef GET_COM_SELECT_INSERT_H
#define GET_COM_SELECT_INSERT_H

#include "testconnections.h"

/**
Reads COM_SELECT and COM_INSERT variables from all nodes and stores into 'selects' and 'inserts'
*/
int get_global_status_allnodes(int *selects, int *inserts, Mariadb_nodes * nodes, int silent);

/**
Prints difference in COM_SELECT and COM_INSERT 
*/
int print_delta(int *new_selects, int *new_inserts, int *selects, int *inserts, int NodesNum);


#endif // GET_COM_SELECT_INSERT_H
