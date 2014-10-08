#ifndef MARIADB_NODES_H
#define MARIADB_NODES_H

#include "mariadb_func.h"

class Mariadb_nodes
{
public:
    Mariadb_nodes(char * pref);
    MYSQL *nodes[256];
    char IP[256][16];
    int N;
    int Master;
    char prefix[16];
    int Connect();
    int CloseConn();
    int ReadEnv();
    int PrintIP();

    int FindMaster();
    int ChangeMaster(int NewMaster, int OldMaster);
};

#endif // MARIADB_NODES_H
