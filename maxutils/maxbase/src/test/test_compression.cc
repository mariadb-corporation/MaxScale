/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/ccdefs.hh>
#include <maxbase/string.hh>
#include <maxbase/random.hh>
#include <maxbase/compress.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/temp_file.hh>

#include <cstdio>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <thread>
#include <future>
#include <unistd.h>

using std::ios_base;
using namespace maxbase;

TempDirectory temp_files("/tmp/pinloki_tmp");

// Level 3 is the zstd default. Level 2 is much faster and
// compresses only slightly less than level 3.
const int COMPRESSION_LEVEL = 2;

template<class S>
void exit_on_error(const S& s)
{
    if (s.status() != maxbase::CompressionStatus::OK)
    {
        std::string comp_err = s.last_comp_error() ? " : "s + s.last_comp_error_str() : "";
        std::cout << to_string(s.status()) << comp_err << std::endl;
        exit(1);
    }
}

void generate_input_data(const TempFile& input)
{
    maxbase::StopWatch sw;
    maxbase::XorShiftRandom rnd;
    std::string chars = "abc ";
    auto os = input.make_stream<std::ofstream>(ios_base::trunc);
    for (size_t i = 0; i < 100 * 1024 * 1024; ++i)
    {
        // not quite random so it compresses well enough, about 2/1.
        os << chars[rnd.b_to_e_co(0, chars.size() - 1)];
    }
    std::cout << "Generate input " << maxbase::to_string(sw.split()) << std::endl;
}

void test_compress(const TempFile& input, const TempFile& compressed)
{
    maxbase::StopWatch sw;
    auto in = input.make_stream<std::ifstream>(ios_base::in);
    auto out = compressed.make_stream<std::ofstream>(ios_base::trunc);
    maxbase::Compressor compressor(COMPRESSION_LEVEL);
    exit_on_error(compressor);
    compressor.compress(in, out);
    exit_on_error(compressor);
    std::cout << "Compress " << maxbase::to_string(sw.split()) << std::endl;
}

void test_decompress(const TempFile& compressed, const TempFile& decompressed)
{
    maxbase::StopWatch sw;
    auto in = compressed.make_stream<std::ifstream>(ios_base::in);
    auto out = decompressed.make_stream<std::ofstream>(ios_base::trunc);
    maxbase::Decompressor decompressor;
    exit_on_error(decompressor);
    decompressor.decompress(in, out);
    exit_on_error(decompressor);
    std::cout << "Decompress " << maxbase::to_string(sw.split()) << std::endl;
}

void test_decompress_async(const TempFile& compressed, const TempFile& verify)
{
    maxbase::StopWatch sw;
    using SD = maxbase::Decompressor;
    TempFile temp_file = temp_files.temp_file();

    auto in = compressed.make_stream<std::ifstream>(ios_base::in);
    auto out = temp_file.make_stream<std::ofstream>(ios_base::out);
    auto async_in = temp_file.make_stream<std::ifstream>(ios_base::in);
    auto verify_out = verify.make_stream<std::ofstream>(ios_base::out);

    SD decompressor;
    exit_on_error(decompressor);

    auto future = std::async(&SD::decompress, &decompressor,
                             std::ref(in), std::ref(out));

    const size_t CHUNK = 1024 * 1024;
    std::array<char, CHUNK> buf;

    for (;;)
    {
        auto to_read = std::min(CHUNK, size_t(async_in.rdbuf()->in_avail()));

        if (to_read > 0)
        {
            async_in.read(buf.data(), to_read);
            verify_out.write(buf.data(), to_read);
        }
        else if (auto future_stat = future.wait_for(1ms);
                 future_stat == std::future_status::ready
                 && async_in.rdbuf()->in_avail() == 0)
        {
            break;
        }
    }

    exit_on_error(decompressor);
    std::cout << "Async decompress " << maxbase::to_string(sw.split()) << std::endl;
}

void compare_files(const TempFile& thing1, const TempFile& thing2)
{
    auto diff = MAKE_STR("diff " << thing1.name() << ' ' << thing2.name().c_str()
                                 << " >/dev/null");

    if (auto ret = system(diff.c_str()); WEXITSTATUS(ret) != 0)
    {
        std::cerr << "\nERROR: File " << thing1.name()
                  << " does not match " << thing2.name()
                  << std::endl;
        exit(WEXITSTATUS(ret));
    }
}

int main()
{
    mxb_log_init(MXB_LOG_TARGET_STDOUT);

    TempFile input = temp_files.temp_file();
    TempFile compressed = temp_files.temp_file();
    TempFile decompressed = temp_files.temp_file();
    TempFile verify = temp_files.temp_file();

    generate_input_data(input);

    test_compress(input, compressed);
    test_decompress(compressed, decompressed);
    compare_files(input, decompressed);

    test_decompress_async(compressed, verify);
    compare_files(input, verify);

    return EXIT_SUCCESS;
}
