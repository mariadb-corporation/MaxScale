#include "fw_copy_rules.h"
#include <sstream>

void copy_rules(TestConnections* Test, const char* rules_name, const char* rules_dir)
{
    std::stringstream src;
    std::stringstream dest;

    src << rules_dir << "/" << rules_name;
    dest << Test->maxscales->access_homedir[0] << "/rules/rules.txt";

    Test->set_timeout(30);
    Test->maxscales->copy_to_node_legacy(src.str().c_str(), dest.str().c_str(), 0);
    Test->stop_timeout();
}
