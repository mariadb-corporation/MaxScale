/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/assert.h>
#include <maxscale/utils.h>
#include <maxscale/utils.hh>
#include <maxbase/random.hh>
#include <string.h>
#include <iostream>

#include "test_utils.hh"

using std::cout;
using std::endl;

namespace
{

template<typename T>
int test_checksums()
{
    uint8_t data[] =
    {
        'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!'
    };

    GWBUF* d1 = gwbuf_alloc_and_load(sizeof(data), data);
    GWBUF* d2 = gwbuf_alloc_and_load(sizeof(data), data);

    T sum1, sum2;
    sum1.update(d1);
    sum1.finalize();
    sum2.finalize(d1);
    mxb_assert(sum1 == sum2);
    sum1.reset();
    sum2.reset();

    // Check that the hex strings match
    mxb_assert(sum1.hex() == sum2.hex());

    std::string saved = sum1.hex();

    // The checksum must not be empty
    mxb_assert(!saved.empty());

    // Repeat the same test, should produce the same checksums
    sum1.update(d1);
    sum1.finalize();
    sum2.finalize(d1);
    mxb_assert(sum1 == sum2);
    mxb_assert(sum1.hex() == saved);
    mxb_assert(sum2.hex() == saved);
    sum1.reset();
    sum2.reset();

    // Check that different buffers but same content produce the same checksum
    sum1.finalize(d2);
    sum2.finalize(d1);
    mxb_assert(sum1 == sum2);
    mxb_assert(sum1.hex() == saved);
    mxb_assert(sum2.hex() == saved);

    gwbuf_free(d1);
    gwbuf_free(d2);

    return 0;
}

int test_base64()
{
    mxb::StdTwisterRandom rnd{123};
    std::vector<uint8_t> data;

    std::string hello_world = "Hello world";
    std::string encoded_hello = "SGVsbG8gd29ybGQ=";
    auto decode_result = mxs::from_base64(encoded_hello);
    std::string hello_result((char*)decode_result.data(), decode_result.size());

    if (hello_result != hello_world)
    {
        std::cout << "Expected '" << hello_world << "', got '" << hello_result << "'\n";
        return 1;
    }

    auto encode_result = mxs::to_base64((uint8_t*)hello_world.c_str(), hello_world.size());

    if (encode_result != encoded_hello)
    {
        std::cout << "Expected '" << encoded_hello << "', got '" << encode_result << "'\n";
        return 1;
    }

    for (int i = 1; i < 1000; i++)
    {
        data.push_back(rnd.rand32());
        auto encoded = mxs::to_base64(data);
        auto decoded = mxs::from_base64(encoded);

        if (decoded != data)
        {
            std::cout << "Original data: " << mxs::to_hex(data.begin(), data.end()) << std::endl;
            std::cout << "Decoded data:  " << mxs::to_hex(decoded.begin(), decoded.end()) << std::endl;
            std::cout << "Base64 data:   " << encoded << std::endl;
            return 1;
        }
    }

    return 0;
}
}

int main(int argc, char* argv[])
{
    int rv = 0;

    init_test_env();
    rv += test_checksums<mxs::SHA1Checksum>();
    rv += test_checksums<mxs::CRC32Checksum>();
    rv += test_base64();

    return rv;
}
