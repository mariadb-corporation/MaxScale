#include "mariadb_nodes.h"
#include "sql_const.h"

Mariadb_nodes::Mariadb_nodes(char * pref)
{
    strcpy(prefix, pref);
}

int Mariadb_nodes::Connect()
{
    for (int i = 0; i < N; i++) {
        nodes[i] = open_conn(3306, IP[i]);
    }
}

int Mariadb_nodes::CloseConn()
{
    for (int i = 0; i < N; i++) {
        if (nodes[i] != NULL) {mysql_close(nodes[i]);}
    }
}

int Mariadb_nodes::ReadEnv()
{
    char * env;
    char env_name[16];
    sprintf(env_name, "%s_N", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%d", &N); } else {N = 0;}

    if ((N > 0) && (N < 255)) {
        for (int i = 0; i < N; i++) {
            sprintf(env_name, "%s_%03d", prefix, i);
            env = getenv(env_name); if (env != NULL) {sprintf(IP[i], "%s", env);}
        }
    }
}

int Mariadb_nodes::PrintIP()
{
    for (int i = 0; i < N; i++) {printf("%s node %d \t%s\n", prefix, i, IP[i]);}
}

int Mariadb_nodes::FindMaster()
{
    char str[255];
    char master_IP[256];
    int i = 0;
    int found = 0;
    int master_node = 255;
    while ((found == 0) && (i < N)) {
        if (find_status_field(
                    nodes[i], (char *) "show slave status;",
                    (char *) "Master_Host", &str[0]
                    ) == 0 ) {
            found = 1;
            strcpy(master_IP, str);
        }
        i++;
    }
    if (found == 1) {
        found = 0; i = 0;
        while ((found == 0) && (i < N)) {
            if (strcmp(IP[i], master_IP) == 0) {
                found = 1;
                master_node = i;
            }
            i++;
        }
    }
    return(master_node);
}

int Mariadb_nodes::ChangeMaster(int NewMaster)
{
    int i;
    int OldMaster = FindMaster();
    char log_file[256];
    char log_pos[256];
    char str[1024];

    for (i = 0; i < N; i++) {
        if (i != OldMaster) {execute_query(nodes[i], (char *) "stop slave;");}
    }
    execute_query(nodes[NewMaster], create_repl_user);
    find_status_field(nodes[NewMaster], (char *) "show master status", (char *) "File", &log_file[0]);
    find_status_field(nodes[NewMaster], (char *) "show master status", (char *) "Position", &log_pos[0]);
    for (i = 0; i < N; i++) {
        if (i != NewMaster) {
            sprintf(str, setup_slave, IP[NewMaster], log_file, log_pos);
            execute_query(nodes[i], str);
        }
    }
    for (i = 0; i < N; i++) {if (i != NewMaster) {execute_query(nodes[i], (char *) "start slave;"); }}
}
