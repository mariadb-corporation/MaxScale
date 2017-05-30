#include "fw_copy_rules.h"

void copy_rules(TestConnections* Test, char * rules_name, char * rules_dir)
{
    Test->set_timeout(30);
    Test->tprintf("Creating rules dir\n");
    Test->ssh_maxscale(true, "rm -rf %s/rules; mkdir %s/rules",
                       Test->maxscale_access_homedir,  Test->maxscale_access_homedir);

    Test->set_timeout(30);
    char str[2048];
    sprintf(str, "scp -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s/%s %s@%s:%s/rules/rules.txt", Test->maxscale_keyfile, rules_dir, rules_name, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_homedir);
    Test->tprintf("Copying rules to Maxscale machine: %s\n", str);
    system(str);

    Test->set_timeout(30);
    Test->tprintf("Copying rules to Maxscale machine\n");
    Test->ssh_maxscale(true, "chown maxscale:maxscale %s/rules -R", Test->maxscale_access_homedir);
}
