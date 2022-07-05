#include "pinloki_encryption.hh"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/install_vault.sh"s, "~/install_vault.sh");
    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/start_vault.sh"s, "~/start_vault.sh");
    test.maxscale->copy_to_node(mxt::SOURCE_DIR + "/pinloki/stop_vault.sh"s, "~/stop_vault.sh");

    auto ret = test.maxscale->ssh_output("./install_vault.sh", false);
    test.expect(ret.rc == 0, "Failed to install Vault: %s", ret.output.c_str());
    ret = test.maxscale->ssh_output("./start_vault.sh", false);
    test.expect(ret.rc == 0, "Failed to start Vault: %s", ret.output.c_str());

    test.maxscale->start();
    auto rv = EncryptionTest(test).result();

    ret = test.maxscale->ssh_output("./stop_vault.sh", false);
    test.expect(ret.rc == 0, "Failed to stop Vault: %s", ret.output.c_str());
    return rv;
}
