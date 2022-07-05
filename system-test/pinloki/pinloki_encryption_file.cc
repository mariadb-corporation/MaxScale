#include "pinloki_encryption.hh"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    // Create the encryption key before MaxScale is started. Use a hard-coded key as the OpenSSL client isn't
    // installed on the test VM.
    const char* key = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    test.maxscale->ssh_node_f(true, "echo -n '1;%s' > /tmp/encryption.key", key);
    test.maxscale->start();

    auto rv = EncryptionTest(test).result();
    test.maxscale->ssh_node_f(true, "rm /tmp/encryption.key");
    return rv;
}
