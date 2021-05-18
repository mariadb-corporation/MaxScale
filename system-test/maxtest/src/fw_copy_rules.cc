#include <maxtest/fw_copy_rules.hh>
#include <sstream>
#include <maxtest/testconnections.hh>

void copy_rules(TestConnections* Test, const char* rules_name, const char* rules_dir)
{
    std::stringstream src;
    std::stringstream dest;

    Test->maxscales->ssh_node_f(0,
                                true,
                                "cd %s;"
                                "rm -rf rules;"
                                "mkdir rules;"
                                "chown %s:%s rules",
                                Test->maxscales->access_homedir(),
                                Test->maxscales->access_user(),
                                Test->maxscales->access_user());

    src << rules_dir << "/" << rules_name;
    dest << Test->maxscales->access_homedir() << "/rules/rules.txt";

    Test->set_timeout(30);
    Test->maxscales->copy_to_node_legacy(src.str().c_str(), dest.str().c_str(), 0);
    Test->maxscales->ssh_node_f(0,
                                true,
                                "chmod a+r %s",
                                dest.str().c_str());
    Test->stop_timeout();
}
