#include "fw_copy_rules.h"
#include <string>

void copy_rules(TestConnections* Test, const char* rules_name, const char* rules_dir)
{
    Test->set_timeout(30);
    Test->ssh_maxscale(true, "cd %s;"
                       "rm -rf rules;"
                       "mkdir rules;"
                       "chown vagrant:vagrant rules -R",
                       Test->maxscale_access_homedir);

    Test->set_timeout(30);

    std::string src;
    std::string dest;

    src += rules_dir;
    src += "/";
    src += rules_name;

    dest += Test->maxscale_access_homedir;
    dest += "/rules/rules.txt";

    Test->copy_to_maxscale(src.c_str(), dest.c_str());

    Test->set_timeout(30);
    Test->ssh_maxscale(true, "chown maxscale:maxscale %s/rules -R", Test->maxscale_access_homedir);
    Test->stop_timeout();
}
