#include "fw_copy_rules.h"
#include <sstream>

void copy_rules(TestConnections* Test, const char* rules_name, const char* rules_dir)
{
    std::stringstream src;
    std::stringstream dest;

    src << rules_dir << "/" << rules_name;
    dest << Test->maxscale_access_homedir << "/rules/rules.txt";

    Test->set_timeout(30);
    Test->copy_to_maxscale(src.str().c_str(), dest.str().c_str());
    Test->stop_timeout();
}
