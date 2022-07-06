#include "pinloki_encryption.hh"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/install_pykmip.sh"s, "~/install_pykmip.sh");
    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/start_pykmip.sh"s, "~/start_pykmip.sh");
    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/stop_pykmip.sh"s, "~/stop_pykmip.sh");

    int rc = test.maxscale->ssh_node_f(false, "./install_pykmip.sh");
    test.expect(rc == 0, "Failed to install PyKMIP");
    rc = test.maxscale->ssh_node_f(false, "./start_pykmip.sh");
    test.expect(rc == 0, "Failed to start PyKMIP");

    test.maxscale->start();
    auto rv = EncryptionTest(test).result();

    rc = test.maxscale->ssh_node_f(false, "./stop_pykmip.sh");
    test.expect(rc == 0, "Failed to stop PyKMIP");
    return rv;
}
