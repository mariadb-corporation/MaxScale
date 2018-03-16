#include "keepalived_func.h"

char * print_version_string(TestConnections * Test)
{
    MYSQL * keepalived_conn = open_conn(Test->maxscales->rwsplit_port[0], virtual_ip, Test->maxscales->user_name, Test->maxscales->password, Test->ssl);
    const char * version_string;
    mariadb_get_info(keepalived_conn, MARIADB_CONNECTION_SERVER_VERSION, (void *)&version_string);
    Test->tprintf("%s\n", version_string);
    mysql_close(keepalived_conn);
    return((char*) version_string);
}

void configure_keepalived(TestConnections* Test, char * keepalived_file)
{
    int i;
    char client_ip[24];
    char * last_dot;
    Test->get_client_ip(0, client_ip);
    last_dot = client_ip;
    Test->tprintf("My IP is %s\n", client_ip);
    for (i = 0; i < 3; i++)
    {
        last_dot = strstr(last_dot, ".");
        last_dot = &last_dot[1];
    }
    last_dot[0] = '\0';
    Test->tprintf("First part of IP is %s\n", client_ip);

    sprintf(virtual_ip, "%s253", client_ip);


    for (i = 0; i < Test->maxscales->N; i++)
    {
        std::string src = std::string(test_dir)
                + "/keepalived_cnf/"
                + std::string(keepalived_file)
                + std::to_string(i + 1)
                + ".conf";
        std::string cp_cmd = "cp "
                + std::string(Test->maxscales->access_homedir[i])
                + std::string(keepalived_file)
                + std::to_string(i + 1) + ".conf "
                + " /etc/keepalived/keepalived.conf";
        Test->tprintf("%s\n", src.c_str());
        Test->tprintf("%s\n", cp_cmd.c_str());
        Test->maxscales->ssh_node(i, "yum install -y keepalived", true);
        Test->maxscales->ssh_node(i, "service iptables stop", true);
        Test->maxscales->copy_to_node(i, src.c_str(), Test->maxscales->access_homedir[i]);
        Test->maxscales->ssh_node(i, cp_cmd.c_str(), true);
        Test->maxscales->ssh_node_f(i, true, "sed -i \"s/###virtual_ip###/%s/\" /etc/keepalived/keepalived.conf", virtual_ip);
        std::string script_src = std::string(test_dir) + "/keepalived_cnf/*.sh";
        std::string script_cp_cmd = "cp " + std::string(Test->maxscales->access_homedir[i]) + "*.sh /usr/bin/";
        Test->maxscales->copy_to_node(i, script_src.c_str(), Test->maxscales->access_homedir[i]);
        Test->maxscales->ssh_node(i, script_cp_cmd.c_str(), true);
        Test->maxscales->ssh_node(i, "sudo service keepalived restart", true);
    }
}
