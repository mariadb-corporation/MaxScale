/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/assert.hh>
#include <maxbase/checksum.hh>
#include <maxbase/externcmd.hh>
#include <maxscale/utils.hh>
#include <maxbase/random.hh>
#include <string.h>
#include <iostream>

#include "test_utils.hh"

using std::cout;
using std::endl;
using mxb::ExternalCmd;
using mxb::Process;

namespace
{

uint8_t data[] =
{
    'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!'
};

template<typename T>
int test_checksums()
{
    GWBUF d1(data, sizeof(data));
    GWBUF d2(data, sizeof(data));

    T sum1;
    T sum2;
    sum1.update(d1);
    sum1.finalize();
    sum2.finalize(d1);
    mxb_assert(sum1 == sum2);

    // Check that the hex strings match
    mxb_assert(sum1.hex() == sum2.hex());

    std::string saved = sum1.hex();

    // The checksum must not be empty
    mxb_assert(!saved.empty());

    sum1.reset();
    sum2.reset();

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

    return 0;
}

template<class Alg>
int test_checksum_result(std::string_view input, std::string_view expected)
{
    int errors = 0;
    Alg c;
    c.finalize(reinterpret_cast<const uint8_t*>(input.data()), input.size());

    if (auto val = c.hex(); val != expected)
    {
        std::cout << "Expected a result of " << expected << " but got " << val << " instead." << std::endl;
        ++errors;
    }

    return errors;
}

int test_base64()
{
    mxb::XorShiftRandom rnd{123};
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
            std::cout << "Original data: " << mxb::to_hex(data.begin(), data.end()) << std::endl;
            std::cout << "Decoded data:  " << mxb::to_hex(decoded.begin(), decoded.end()) << std::endl;
            std::cout << "Base64 data:   " << encoded << std::endl;
            return 1;
        }
    }

    return 0;
}

template<class Result, class Expected>
int compare(Result result, Expected expected)
{
    int errors = 0;

    if (result != expected)
    {
        std::cout << "Result is '" << result << "' instead of '" << expected << "'\n";
        ++errors;
    }

    return errors;
}

int test_externcmd()
{
    int errors = 0;
    std::string result;
    auto handler = [&](auto cmd, auto line){
        result = line;
    };

    auto cmd = ExternalCmd::create("/usr/bin/env echo hello", 5, handler);

    cmd->run();
    errors += compare(result, "hello");

    cmd = ExternalCmd::create("/usr/bin/env echo world", 5, handler);
    cmd->start();
    cmd->wait();
    errors += compare(result, "world");

    cmd = ExternalCmd::create("/bin/sh -c 'sleep 1; echo hello world'", 30, handler);
    cmd->start();

    int rc = Process::ERROR;
    auto start = mxb::Clock::now();

    while ((rc = cmd->try_wait()) == Process::TIMEOUT && start - mxb::Clock::now() < 30s)
    {
        std::this_thread::sleep_for(50ms);
    }

    errors += compare(rc, 0);
    errors += compare(result, "hello world");

    cmd = ExternalCmd::create("/bin/cat", 30, handler);
    cmd->start();
    std::string msg = "echo";
    cmd->write(msg.c_str(), msg.size());
    cmd->close_output();
    rc = cmd->wait();

    errors += compare(rc, 0);
    errors += compare(result, "echo");

    std::vector<std::string> results;
    std::vector<std::string> expected;
    cmd = ExternalCmd::create("/bin/cat", 30, [&](auto ignored, auto line){
        results.push_back(line);
    });
    cmd->start();

    for (int i = 0; i < 123456; i++)
    {
        std::string num = std::to_string(i) + "\n";
        cmd->write(num.c_str(), num.size());
        // The output gets trimmed by the ExternalCmd
        mxb::trim(num);
        expected.push_back(num);
    }

    cmd->close_output();
    rc = cmd->wait();

    errors += compare(rc, 0);
    errors += compare(mxb::join(results), mxb::join(expected));

    return errors;
}
}

int main(int argc, char* argv[])
{
    int rv = 0;

    init_test_env();
    rv += test_checksums<mxb::Sha1Sum>();
    rv += test_checksums<mxb::CRC32>();
    rv += test_checksums<mxb::xxHash>();
    rv += test_checksum_result<mxb::CRC32>("hello world", "85114a0d");
    rv += test_checksum_result<mxb::Sha1Sum>("hello world", "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
    rv += test_checksum_result<mxb::xxHash>("hello world", "c7b615cc75879ba90049873fe9098ddf");
    rv += test_base64();
    rv += test_externcmd();

    return rv;
}
