/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/secrets.hh>
#include <maxbase/log.hh>

#include <vector>
#include <map>
#include <iostream>
#include <numeric>

#include <openssl/rand.h>

std::vector<int> lengths = {
    // A handful of prime numbers for testing encryption of some lengths
    2, 3, 5, 7,  11, 13, 17,  19,  23,  29,   31,   37,   41,  43,  47,  53,  59,   61,   67, 71,
    // Powers of two
    2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
    // Fibonacci numbers
    1, 1, 2, 3,  5,  8,  13,  21,  34,  55,   89,   144,  233, 377, 610, 987, 1597, 2584,
};

int main()
{
    mxb_log_init(MXB_LOG_TARGET_STDOUT);

    std::vector modes {mxb::Cipher::AES_CBC, mxb::Cipher::AES_CTR, mxb::Cipher::AES_GCM};
    std::vector bits {128, 192, 256};

    std::vector<std::pair<std::string, std::string>> failed;

    std::cout << "Cipher\tBlock\tKey\tIV" << std::endl;

    for (const auto& m : modes)
    {
        for (int b : bits)
        {
            std::stringstream ss;
            std::stringstream reason;
            mxb::Cipher cipher(m, b);
            std::vector<uint8_t> key = cipher.new_key();
            std::vector<uint8_t> iv = cipher.new_iv();

            std::cout << cipher.to_string() << '\t'
                      << cipher.block_size() << '\t'
                      << cipher.key_size() << '\t'
                      << cipher.iv_size() << '\t'
                      << std::endl;

            const char* indent = "  ";
            bool error = false;

            for (int l : lengths)
            {
                size_t bs = cipher.block_size();
                std::vector<uint8_t> in(l);
                std::vector<uint8_t> out(cipher.encrypted_size(l));

                std::iota(in.begin(), in.end(), 0);

                ss << indent << "Plaintext size: " << in.size() << std::endl;

                int out_size = 0;
                bool ok = cipher.encrypt(key.data(), iv.data(), in.data(), in.size(), out.data(), &out_size);
                ss << indent << "Encrypt: " << (ok ? "OK" : "ERR") << std::endl;
                ss << indent << "Actual size: " << out_size << std::endl;

                if (!ok)
                {
                    error = true;
                    reason << "Encrypt error" << std::endl;
                }

                int calculated_size = cipher.encrypted_size(l);
                ss << indent << "Calculated size: " << calculated_size << std::endl;
                ok = calculated_size == out_size;
                ss << indent << "Size: " << (ok ? "OK" : "ERR") << std::endl;

                if (!ok)
                {
                    error = true;
                    reason << "Encrypt size mismatch" << std::endl;
                }

                std::vector<uint8_t> out2;
                out2.resize(out.size());

                int out2_size = 0;
                ok = cipher.decrypt(key.data(), iv.data(), out.data(), out_size, out2.data(), &out2_size);
                ss << indent << "Decrypt: " << (ok ? "OK" : "ERR") << std::endl;
                out2.resize(out2_size);

                if (!ok)
                {
                    error = true;
                    reason << "Decrypt error" << std::endl;
                }

                ok = in == out2;
                ss << indent << "Equal: " << (ok ? "OK" : "ERR") << std::endl;

                if (!ok)
                {
                    error = true;
                    reason << "Data not equal" << std::endl;
                }

                if (m == mxb::Cipher::AES_GCM)
                {
                    // Flip the bits in the second byte. This tests the authenticated part of the
                    // authenticated encryption modes.
                    out[2] = ~out[2];

                    ok = !cipher.decrypt(key.data(), iv.data(), out.data(), out_size,
                                         out2.data(), &out2_size);
                    ss << indent << "Decrypt malformed: " << (ok ? "OK" : "ERR") << std::endl;

                    if (!ok)
                    {
                        error = true;
                        reason << "Decrypt malformed error" << std::endl;
                    }
                }

                ss << std::endl;

                if (!ok)
                {
                    ss << cipher.get_errors();
                }
            }

            ss << "-------------------------------" << std::endl;

            if (error)
            {
                failed.emplace_back(cipher.to_string(), reason.str());
                std::cout << ss.str() << std::endl;
            }
        }
    }

    if (!failed.empty())
    {
        std::cout << "Failed: " << std::endl;
    }

    for (auto f : failed)
    {
        std::cout << f.first << std::endl;
    }

    return failed.empty() ? 0 : 1;
}
