/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/temp_file.hh>
#include <maxbase/compress.hh>
#include <maxbase/exception.hh>

#include <fstream>
#include <future>
#include <utility>

namespace pinloki
{

class BinlogFile final
{
    enum LocalStat
    {
        REGULAR_FILE,
        DECOMPRESSING,
        FAILED,
        DECOMPRESS_DONE
    };

public:
    // Throws BinlogReadError.
    // First remove the compression extension from file_name,
    // if any, and try to open the file. If that fails,
    // try to open file name + compression extension(s).
    BinlogFile(std::string_view file_name);
    ~BinlogFile();
    BinlogFile(BinlogFile&&) = delete;

    // Throws BinlogReadError.
    // As long as check_status returns true, continue to call in a
    // polling manner as decompression is ongoing and an error
    // can happen while files are being read/written.
    bool check_status();

    // Throws BinlogReadError.
    // Create an ifstream to read from.
    std::ifstream make_ifstream();

    // The actual name of the file that is opened for reading by
    // make_ifstream(). This is a temp file if decompression
    // is ongoing.
    const std::string& file_name() const;

private:
    LocalStat     m_local_stat = FAILED;
    bool          m_is_compressed = false;
    std::string   m_input_file;
    std::string   m_output_file;
    mxb::TempFile m_temp_file;
    std::ifstream m_compressed_in;
    std::ofstream m_decompressed_out;

    std::unique_ptr<maxbase::Decompressor> m_sDecompressor;
    std::future<mxb::CompressionStatus>    m_future;

    struct OpenRes
    {
        LocalStat     local_stat;
        std::string   file_name;
        std::ifstream in_stream;
    };

    OpenRes open_file(std::string_view file_name);
};

inline const std::string& BinlogFile::file_name() const
{
    return m_output_file;
}

inline std::ifstream BinlogFile::make_ifstream()
{
    check_status();
    return std::ifstream{m_output_file};
}
}
