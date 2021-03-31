/**
 * @file kerberos_setup.cpp Attempt to configure KDC and try to use passwordless authentification
 * - configure KDC on Maxscale machine and Kerberos workstation on all other nodes
 * - create MariaDB user which is authentificated via GSSAPI
 * - try to login to Maxscale as this GSSAPI user and execute simple query
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int install_kerberos(std::string machine_name, std::string maria_version)
{
    int res = system((std::string("mdbci install_product --product kerberos_server ") + machine_name).c_str());
    // Ignoring exit code becase in some versions of MariaDB gssapi is included into client/server
    system((std::string("mdbci install_product --product plugin_gssapi_client --product-version ") + maria_version + " " + machine_name).c_str());
    system((std::string("mdbci install_product --product plugin_gssapi_server --product-version ") + maria_version + " " + machine_name).c_str());
    return res;
}

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(1000);
    char str[1024];
    char str1[1024];

    int i;

    // To be moved to MDBCI
    Test->tprintf("Creating 'hosts' file\n");
    FILE* f;
    f = fopen("hosts", "wt");
    for (i = 0; i < Test->repl->N; i++)
    {
        fprintf(f, "%s node_%03d.maxscale.test\n", Test->repl->ip4(i), i);
        fprintf(f, "%s node_%03d\n", Test->repl->ip4(i), i);
    }
    fprintf(f, "%s maxscale.maxscale.test\n", Test->maxscales->ip4(0));
    fprintf(f, "%s maxscale\n", Test->maxscales->ip4(0));
    fclose(f);

    Test->tprintf(
        "Copying 'hosts' and krb5.conf files to all nodes, installing kerberos client and MariaDB plugins\n");
    sprintf(str, "%s/krb5.conf", test_dir);
    for (i = 0; i < Test->repl->N; i++)
    {
        std::string machine_name = Test->get_mdbci_config_name() + "/" + Test->repl->mdbci_node_name(i);
        std::string maria_vrs = Test->repl->backend(i)->version_as_string();
        install_kerberos(machine_name, maria_vrs);
        auto homedir = Test->repl->access_homedir(i);
        Test->repl->copy_to_node_legacy(str, homedir, i);
        sprintf(str1, "cp %s/krb5.conf /etc/", homedir);
        Test->repl->copy_to_node_legacy(str, Test->repl->access_homedir(i), i);
        sprintf(str1, "cp %s/krb5.conf /etc/", Test->repl->access_homedir(i));
        Test->repl->ssh_node(i, str1, true);

        Test->repl->copy_to_node_legacy("hosts", homedir, i);
        sprintf(str1, "cp %s/hosts /etc/", homedir);
        Test->repl->ssh_node(i, str1, true);
    }

    Test->tprintf("Copying 'hosts' and krb5.conf files to Maxscale node\n");

    auto mxs_homedir = Test->maxscales->access_homedir(0);
    Test->maxscales->copy_to_node_legacy("hosts", mxs_homedir, 0);
    Test->maxscales->ssh_node_f(0, true, "cp %s/hosts /etc/", mxs_homedir);

    Test->maxscales->copy_to_node_legacy(str, mxs_homedir, 0);
    Test->maxscales->ssh_node_f(0, true, "cp %s/krb5.conf /etc/", mxs_homedir);

    Test->tprintf("Instaling Kerberos server packages to Maxscale node\n");
    std::string machine_name = Test->get_mdbci_config_name() + "/" + Test->maxscales->mdbci_node_name(0);
    std::string maria_vrs = Test->repl->backend(0)->version_as_string();
    install_kerberos(machine_name, maria_vrs);

    Test->maxscales->ssh_node(0, (char*) "rngd -r /dev/urandom -o /dev/random", true);

    Test->tprintf("Configuring Kerberos server\n");
    Test->maxscales->ssh_node(0,
                              (char*)
                              "sed -i \"s/EXAMPLE.COM/MAXSCALE.TEST/\" /var/kerberos/krb5kdc/kdc.conf",
                              true);
    Test->maxscales->ssh_node(0,
                              (char*)
                              "sed -i \"s/EXAMPLE.COM/MAXSCALE.TEST/\" /var/kerberos/krb5kdc/kadm5.acl",
                              true);
    Test->tprintf("Creating Kerberos DB and admin principal\n");
    Test->maxscales->ssh_node(0, (char*) "kdb5_util create -P skysql -r MAXSCALE.TEST -s", true);
    Test->maxscales->ssh_node(0,
                              (char*) "kadmin.local -q \"addprinc -pw skysql admin/admin@MAXSCALE.TEST\"",
                              true);

    Test->tprintf("Opening ports 749 and 88\n");
    Test->maxscales->ssh_node(0, (char*) "iptables -I INPUT -p tcp --dport 749 -j ACCEPT", true);
    Test->maxscales->ssh_node(0, (char*) "iptables -I INPUT -p tcp --dport 88 -j ACCEPT", true);

    Test->tprintf("Starting Kerberos\n");
    Test->maxscales->ssh_node(0, (char*) "service krb5kdc start", true);
    Test->maxscales->ssh_node(0, (char*) "service kadmin start", true);

    Test->tprintf("Creating principal\n");
    Test->maxscales->ssh_node(0,
                              (char*)
                              "echo \"skysql\" | sudo kadmin -p admin/admin -q \"addprinc -randkey mariadb/maxscale.test\"",
                              true);

    Test->tprintf("Creating keytab file\n");
    Test->maxscales->ssh_node(0,
                              (char*)
                              "echo \"skysql\" | sudo kadmin -p admin/admin -q \"ktadd mariadb/maxscale.test\"",
                              true);

    Test->tprintf("Making keytab file readable for all\n");
    Test->maxscales->ssh_node(0, (char*) "chmod a+r /etc/krb5.keytab;", true);

    Test->maxscales->ssh_node(0,
                              (char*) "kinit mariadb/maxscale.test@MAXSCALE.TEST -k -t /etc/krb5.keytab",
                              false);
    Test->maxscales->ssh_node(0,
                              (char*)
                              "mkdir -p /home/maxscale",
                              true);
    Test->maxscales->ssh_node(0,
                              (char*)
                              "su maxscale --login -s /bin/sh -c \"kinit mariadb/maxscale.test@MAXSCALE.TEST -k -t /etc/krb5.keytab\"",
                              true);

    Test->tprintf("Coping keytab file from Maxscale node\n");
    Test->maxscales->copy_from_node_legacy((char*) "/etc/krb5.keytab", (char*) ".", 0);

    Test->tprintf("Coping keytab and .cnf files to all nodes and executing knit for all nodes\n");
    for (i = 0; i < Test->repl->N; i++)
    {
        sprintf(str, "%s/kerb.cnf", test_dir);
        auto homedir = Test->repl->access_homedir(i);
        Test->repl->copy_to_node_legacy(str, homedir, i);
        Test->repl->ssh_node_f(i, true, "cp %s/kerb.cnf /etc/my.cnf.d/", homedir);

        Test->repl->copy_to_node_legacy((char*) "krb5.keytab", homedir, i);
        Test->repl->ssh_node(i, (char*) "cp ~/krb5.keytab /etc/", true);
        Test->repl->ssh_node_f(i, true, "cp %s/krb5.keytab /etc/", homedir);

        Test->repl->ssh_node(i,
                             (char*) "kinit mariadb/maxscale.test@MAXSCALE.TEST -k -t /etc/krb5.keytab",
                             false);
    }

    Test->tprintf("Installing gssapi plugin to all nodes\n");
    Test->repl->connect();
    Test->repl->execute_query_all_nodes((char*) "INSTALL SONAME 'auth_gssapi'");
    Test->repl->close_connections();

    Test->tprintf("Creating usr1 user\n");
    Test->repl->connect();
    Test->try_query(Test->repl->nodes[0],
                    (char*) "CREATE USER usr1 IDENTIFIED VIA gssapi AS 'mariadb/maxscale.test@MAXSCALE.TEST'");
    Test->try_query(Test->repl->nodes[0], (char*) "grant all privileges on  *.* to 'usr1'");
    Test->repl->close_connections();

    Test->tprintf("Trying use usr1 to execute query: RW Split\n");
    Test->add_result(Test->repl->ssh_node(1,
                                          "echo select User,Host from mysql.user | mysql --ssl -uusr1 -h maxscale.maxscale.test -P 4006",
                                          false),
                     "Error executing query against RW Split\n");
    Test->tprintf("Trying use usr1 to execute query: Read Connection Master\n");
    Test->add_result(Test->repl->ssh_node(1,
                                          "echo select User,Host from mysql.user | mysql --ssl -uusr1 -h maxscale.maxscale.test -P 4008",
                                          false),
                     "Error executing query against Read Connection Master\n");
    Test->tprintf("Trying use usr1 to execute query: Read Connection Slave\n");
    Test->add_result(Test->repl->ssh_node(1,
                                          "echo select User,Host from mysql.user | mysql --ssl -uusr1 -h maxscale.maxscale.test -P 4009",
                                          false),
                     "Error executing query against Read Connection Slave\n");

    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->repl->ssh_node(i, "sudo rm -f /etc/my.cnf.d/kerb.cnf", true);
    }

    Test->repl->connect();
    Test->try_query(Test->repl->nodes[0], "DROP USER usr1");
    Test->repl->disconnect();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
