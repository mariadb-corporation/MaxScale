/**
 * @file kerberos_setup.cpp Attempt to configure KDC and try to use passwordless authentification
 * - configure KDC on Maxscale machine and Kerberos workstation on all other nodes
 * - create MariaDB user which is authentificated via GSSAPI
 * - try to login to Maxscale as this GSSAPI user and execute simple query
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(1000);
    char str[1024];
    char str1[1024];

    int i;

    // To be moved to MDBCI
    Test->tprintf("Creating 'hosts' file\n");
    FILE * f;
    f = fopen("hosts", "wt");
    for (i = 0; i < Test->repl->N; i++)
    {
        fprintf(f, "%s node_%03d.maxscale.test\n", Test->repl->IP[i], i);
        fprintf(f, "%s node_%03d\n", Test->repl->IP[i], i);
    }
    fprintf(f, "%s maxscale.maxscale.test\n", Test->maxscale_IP);
    fprintf(f, "%s maxscale\n", Test->maxscale_IP);
    fclose(f);

    Test->tprintf("Copying 'hosts' and krb5.conf files to all nodes, installing kerberos client and MariaDB plugins\n");
    sprintf(str, "%s/krb5.conf", test_dir);
    for (i = 0; i < Test->repl->N; i++)
    {
        Test->repl->ssh_node(i, (char *)
                             "yum clean all", true);
        Test->repl->ssh_node(i, (char *)
                             "yum install -y MariaDB-gssapi-server MariaDB-gssapi-client krb5-workstation pam_krb5", true);
        Test->repl->copy_to_node(str, (char *) "~/", i);
        sprintf(str1, "cp %s/krb5.conf /etc/", Test->repl->access_homedir[i]);
        Test->repl->ssh_node(i, str1, true);

        Test->repl->copy_to_node((char *) "hosts", (char *) "~/", i);
        sprintf(str1, "cp %s/hosts /etc/", Test->repl->access_homedir[i]);
        Test->repl->ssh_node(i, str1, true);
    }

    Test->tprintf("Copying 'hosts' and krb5.conf files to Maxscale node\n");

    Test->copy_to_maxscale((char *) "hosts", (char *) "~/");
    Test->ssh_maxscale(true,  (char *) "cp %s/hosts /etc/", Test->maxscale_access_homedir);

    Test->copy_to_maxscale(str, (char *) "~/");
    Test->ssh_maxscale(true,  (char *) "cp %s/krb5.conf /etc/", Test->maxscale_access_homedir);

    Test->tprintf("Instaling Kerberos server packages to Maxscale node\n");
    Test->ssh_maxscale(true, (char *) "yum clean all");
    Test->ssh_maxscale(true, (char *) "yum install rng-tools -y");
    Test->ssh_maxscale(true, (char *) "rngd -r /dev/urandom -o /dev/random");

    Test->ssh_maxscale(true, (char *)
                       "yum install -y MariaDB-gssapi-server MariaDB-gssapi-client krb5-server krb5-workstation pam_krb5");

    Test->tprintf("Configuring Kerberos server\n");
    Test->ssh_maxscale(true, (char *) "sed -i \"s/EXAMPLE.COM/MAXSCALE.TEST/\" /var/kerberos/krb5kdc/kdc.conf");
    Test->ssh_maxscale(true, (char *) "sed -i \"s/EXAMPLE.COM/MAXSCALE.TEST/\" /var/kerberos/krb5kdc/kadm5.acl");
    Test->tprintf("Creating Kerberos DB and admin principal\n");
    Test->ssh_maxscale(true, (char *) "kdb5_util create -P skysql -r MAXSCALE.TEST -s");
    Test->ssh_maxscale(true, (char *) "kadmin.local -q \"addprinc -pw skysql admin/admin@MAXSCALE.TEST\"");

    Test->tprintf("Opening ports 749 and 88\n");
    Test->ssh_maxscale(true, (char *) "iptables -I INPUT -p tcp --dport 749 -j ACCEPT");
    Test->ssh_maxscale(true, (char *) "iptables -I INPUT -p tcp --dport 88 -j ACCEPT");

    Test->tprintf("Starting Kerberos\n");
    Test->ssh_maxscale(true, (char *) "service krb5kdc start");
    Test->ssh_maxscale(true, (char *) "service kadmin start");

    Test->tprintf("Creating principal\n");
    Test->ssh_maxscale(true, (char *)
                       "echo \"skysql\" | sudo kadmin -p admin/admin -q \"addprinc -randkey mariadb/maxscale.test\"");

    Test->tprintf("Creating keytab file\n");
    Test->ssh_maxscale(true, (char *)
                       "echo \"skysql\" | sudo kadmin -p admin/admin -q \"ktadd mariadb/maxscale.test\"");

    Test->tprintf("Making keytab file readable for all\n");
    Test->ssh_maxscale(true, (char *) "chmod a+r /etc/krb5.keytab;");

    Test->ssh_maxscale(false, (char *) "kinit mariadb/maxscale.test@MAXSCALE.TEST -k -t /etc/krb5.keytab");
    Test->ssh_maxscale(true, (char *)
                       "su maxscale --login -s /bin/sh -c \"kinit mariadb/maxscale.test@MAXSCALE.TEST -k -t /etc/krb5.keytab\"");

    Test->tprintf("Coping keytab file from Maxscale node\n");
    Test->copy_from_maxscale((char *) "/etc/krb5.keytab", (char *) ".");

    Test->tprintf("Coping keytab and .cnf files to all nodes and executing knit for all nodes\n");
    for (i = 0; i < Test->repl->N; i++)
    {
        sprintf(str, "%s/kerb.cnf", test_dir);
        Test->repl->copy_to_node(str, (char *) "~/", i);
        Test->repl->ssh_node(i, (char *) "cp ~/kerb.cnf /etc/my.cnf.d/", true);

        Test->repl->copy_to_node((char *) "krb5.keytab", (char *) "~/", i);
        Test->repl->ssh_node(i, (char *) "cp ~/krb5.keytab /etc/", true);

        Test->repl->ssh_node(i, (char *) "kinit mariadb/maxscale.test@MAXSCALE.TEST -k -t /etc/krb5.keytab", false);
    }

    Test->tprintf("Installing gssapi plugin to all nodes\n");
    Test->repl->connect();
    Test->repl->execute_query_all_nodes((char *) "INSTALL SONAME 'auth_gssapi'");
    Test->repl->close_connections();

    Test->tprintf("Creating usr1 user\n");
    Test->repl->connect();
    Test->try_query(Test->repl->nodes[0],
                    (char *) "CREATE USER usr1 IDENTIFIED VIA gssapi AS 'mariadb/maxscale.test@MAXSCALE.TEST'");
    Test->try_query(Test->repl->nodes[0], (char *) "grant all privileges on  *.* to 'usr1'");
    Test->repl->close_connections();

    Test->tprintf("Trying use usr1 to execute query: RW Split\n");
    Test->add_result(
        Test->repl->ssh_node(1,
                             "echo select User,Host from mysql.user | mysql -uusr1 -h maxscale.maxscale.test -P 4006", false),
        "Error executing query against RW Split\n");
    Test->tprintf("Trying use usr1 to execute query: Read Connection Master\n");
    Test->add_result(
        Test->repl->ssh_node(1,
                             "echo select User,Host from mysql.user | mysql -uusr1 -h maxscale.maxscale.test -P 4008", false),
        "Error executing query against Read Connection Master\n");
    Test->tprintf("Trying use usr1 to execute query: Read Connection Slave\n");
    Test->add_result(
        Test->repl->ssh_node(1,
                             "echo select User,Host from mysql.user | mysql -uusr1 -h maxscale.maxscale.test -P 4009", false),
        "Error executing query against Read Connection Slave\n");

    int rval = Test->global_result;
    delete Test;
    return rval;
}

