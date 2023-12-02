/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <string>
#include <maxscale/utils.hh>
#include "../ed25519_auth.hh"
#include "../ref10/exports/api.h"
#include "../ref10/exports/crypto_sign.h"

using std::string;

namespace
{
struct TestCase
{
    string pw;
    string pubkey;
};

void gen_random_arr(mxb::XorShiftRandom& rnd, size_t outlen, uint8_t* out)
{
    size_t j = 0;
    while (j < outlen)
    {
        uint64_t rand_num = rnd.rand();
        long bytes_left = outlen - j;
        auto write_len = std::min(bytes_left, (long)sizeof(rand_num));
        memcpy(out + j, &rand_num, write_len);
        j += write_len;
    }
}

bool test_signature_gen_check(const string& pw, const uint8_t* pubkey, const uint8_t* message,
                              size_t message_len)
{
    auto sign_buf_len = CRYPTO_BYTES + message_len;
    uint8_t signature_buf[sign_buf_len];
    auto pw_ptr = reinterpret_cast<const unsigned char*>(pw.c_str());
    crypto_sign(signature_buf, message, message_len, pw_ptr, pw.length());

    uint8_t work_arr[sign_buf_len];
    return crypto_sign_open(work_arr, signature_buf, sign_buf_len, pubkey) == 0;
}

int test(const TestCase& test)
{
    // Similar to Ed25519ClientAuthenticator::sha_check_cleartext_pw.
    unsigned char pubkey[CRYPTO_PUBLICKEYBYTES];
    auto* ptr = reinterpret_cast<const unsigned char*>(test.pw.c_str());
    crypto_sign_keypair(pubkey, ptr, test.pw.size());
    string pubkey64 = mxs::to_base64(pubkey, sizeof(pubkey));
    pubkey64.pop_back();

    if (pubkey64 != test.pubkey)
    {
        MXB_ERROR("Wrong public key generated from password '%s'. Expected '%s', got '%s'.",
                  test.pw.c_str(), test.pubkey.c_str(), pubkey64.c_str());
        return 1;
    }

    int fails = 0;
    mxb::XorShiftRandom rnd;
    for (int i = 0; i < 100; i++)
    {
        // Generate random messages and check that signature generation and check matches.
        auto len = rnd.b_to_e_co(0, 31);
        uint8_t message[len > 0 ? len : 1];
        gen_random_arr(rnd, len, message);

        if (!test_signature_gen_check(test.pw, pubkey, message, len))
        {
            MXB_ERROR("test_signature_gen_check() failed.");
            fails++;
        }
    }

    // Generate a few random public keys, they should fail signature check.
    for (int i = 0; i < 10; i++)
    {
        gen_random_arr(rnd, sizeof(pubkey), pubkey);

        auto len = rnd.b_to_e_co(0, 31);
        uint8_t message[len > 0 ? len : 1];
        gen_random_arr(rnd, len, message);

        if (test_signature_gen_check(test.pw, pubkey, message, len))
        {
            MXB_ERROR("test_signature_gen_check() succeeded when it should have failed.");
            fails++;
        }
    }

    return fails;
}
}

int main(int argc, char** argv)
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);

    string pubkey1 = "ZIgUREUg5PVgQ6LskhXmO+eZLS0nC8be6HPjYWR4YJY";
    string pubkey2 = "7fErJC9nfmMvBWzveq259/P8jIdZ0IfoBPuEZo2pIso";

    TestCase tests[] = {
        {"secret",                 "ZIgUREUg5PVgQ6LskhXmO+eZLS0nC8be6HPjYWR4YJY"},
        {"&%#=gr3at_p455w0rD??.,", "7fErJC9nfmMvBWzveq259/P8jIdZ0IfoBPuEZo2pIso"},
        {"", "4LH+dBF+G5W2CKTyId8xR3SyDqZoQjUNUVNxx8aWbG4"},
        {"12345678910", "ezgDNoRK3sfq59G1P532fpwotUGzGxkFxdRcST6uqsM"},
        {"vnuwaiyt493phgoölajsf849yhtiuhndjknvea78ty49peahtjdnfu4hty8974heanfgkui4thai4er,.-'¨",
         "ldLQsjYnV3ALPQ6Ru1z0f6gAIZrK2ssM1KYuo3/vteE"},
    };

    int rval = 0;
    for (auto& t : tests)
    {
        rval += test(t);
    }
    return rval;
}
