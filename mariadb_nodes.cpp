
/**
 * @file mariadb_nodes.cpp - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 17/11/14	Timofey Turenko	Initial implementation
 *
 * @endverbatim
 */

#include "mariadb_nodes.h"
#include "sql_const.h"

Mariadb_nodes::Mariadb_nodes(char * pref)
{
    strcpy(prefix, pref);
}

int Mariadb_nodes::Connect()
{
    for (int i = 0; i < N; i++) {
        nodes[i] = open_conn(Ports[i], IP[i], User, Password);
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
    char env_name[64];
    sprintf(env_name, "%s_N", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%d", &N); } else {N = 0;}

    sprintf(env_name, "%s_User", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%s", User); } else {sprintf(User, "skysql"); }
    sprintf(env_name, "%s_Password", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%s", Password); } else {sprintf(Password, "skysql"); }


    if ((N > 0) && (N < 255)) {
        for (int i = 0; i < N; i++) {
            //reading IPs
            sprintf(env_name, "%s_%03d", prefix, i);
            env = getenv(env_name); if (env != NULL) {sprintf(IP[i], "%s", env);}

            //reading ports
            sprintf(env_name, "%s_port_%03d", prefix, i);
            env = getenv(env_name); if (env != NULL) {
                sscanf(env, "%d", &Ports[i]);
            } else {
                Ports[i] = 3306;
            }
            //reading sshkey
            sprintf(env_name, "%s_sshkey_%03d", prefix, i);
            env = getenv(env_name); if (env != NULL) {sprintf(sshkey[i], "%s", env);}


        }
    }
}

int Mariadb_nodes::PrintIP()
{
    for (int i = 0; i < N; i++) {printf("%s node %d \t%s\tPort=%d\n", prefix, i, IP[i], Ports[i]);}
    printf("%s User name %s\n", prefix, User);
    printf("%s Password %s\n", prefix, Password);
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

int Mariadb_nodes::ChangeMaster(int NewMaster, int OldMaster)
{
    int i;
    //int OldMaster = FindMaster();
    char log_file[256];
    char log_pos[256];
    char str[1024];

    for (i = 0; i < N; i++) {
        if (i != OldMaster) {execute_query(nodes[i], (char *) "stop slave;");}
    }
    execute_query(nodes[NewMaster], create_repl_user);
    execute_query(nodes[OldMaster], (char *) "reset master;");
    find_status_field(nodes[NewMaster], (char *) "show master status", (char *) "File", &log_file[0]);
    find_status_field(nodes[NewMaster], (char *) "show master status", (char *) "Position", &log_pos[0]);
    for (i = 0; i < N; i++) {
        if (i != NewMaster) {
            sprintf(str, setup_slave, IP[NewMaster], log_file, log_pos, Ports[NewMaster]);
            execute_query(nodes[i], str);
        }
    }
    //for (i = 0; i < N; i++) {if (i != NewMaster) {execute_query(nodes[i], (char *) "start slave;"); }}
}

int Mariadb_nodes::StopNodes()
{
    int i;
    int global_result = 0;
    char sys1[4096];
    Connect();
    for (i = 0; i < N; i++) {
        printf("Stopping slave %d\n", i); fflush(stdout);
        execute_query(nodes[i], (char *) "stop slave;");
        printf("Stopping %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql stop'", sshkey[i], IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        global_result += system(sys1); fflush(stdout);
    }
    //CloseConn();
    return(global_result);
}

int Mariadb_nodes::StartReplication()
{
    char sys1[4096];
    char str[1024];
    char log_file[256];
    char log_pos[256];
    int i;
    int global_result = 0;
    global_result += StopNodes();

    printf("Starting back Master\n");  fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start'", sshkey[0], IP[0]);
    printf("%s\n", sys1);  fflush(stdout);
    global_result +=  system(sys1); fflush(stdout);

    for (i = 1; i < N; i++) {
        printf("Starting node %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start '", sshkey[i], IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        global_result += system(sys1); fflush(stdout);
    }
    sleep(5);

    global_result += Connect();
    global_result += execute_query(nodes[0], create_repl_user);

    find_status_field(nodes[0], (char *) "show master status", (char *) "File", &log_file[0]);
    find_status_field(nodes[0], (char *) "show master status", (char *) "Position", &log_pos[0]);
    for (i = 1; i < N; i++) {
        sprintf(str, setup_slave, IP[0], log_file, log_pos, Ports[0]);
        global_result += execute_query(nodes[i], str);
    }
    CloseConn();
    return(global_result);
}
