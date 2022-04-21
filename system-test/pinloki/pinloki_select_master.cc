#include <maxtest/testconnections.hh>
#include "pinloki_select_master.hh"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return MasterSelectTest(test).result();
}
