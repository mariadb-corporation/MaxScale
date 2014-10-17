#ifndef MARIADB_NODES_H
#define MARIADB_NODES_H

#include "mariadb_func.h"

class Mariadb_nodes
{
public:
    Mariadb_nodes(char * pref);
    MYSQL *nodes[256];
    char IP[256][16];
    int Ports[256];
    int N;
    int Master;
    char prefix[16];
    int Connect();
    int CloseConn();
    int ReadEnv();
    int PrintIP();

    char User[256];
    char Password[256];

    int FindMaster();
    int ChangeMaster(int NewMaster, int OldMaster);
};

#endif // MARIADB_NODES_H
