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

#include "binlog_file.hh"
#include "config.hh"

namespace pinloki
{

BinlogFile::BinlogFile(std::string_view file_name)
    : m_temp_file{Config::pinloki_temp_dir().temp_file()}
{
    auto res = open_file(file_name);
    m_local_stat = res.local_stat;
    m_input_file = res.file_name;

    if (m_local_stat == FAILED)
    {
        MXB_THROW(BinlogReadError,
                  "Could not open '" << file_name << "' for reading: "
                                     << errno << ", " << mxb_strerror(errno));
    }

    if (m_local_stat == DECOMPRESSING)
    {
        m_compressed_in = std::move(res.in_stream);
        m_decompressed_out = m_temp_file.make_stream<std::ofstream>(std::ios_base::out);
        if (!m_decompressed_out)
        {
            MXB_THROW(BinlogReadError,
                      "Could not open temp file '" << m_temp_file.name()
                                                   << "': " << errno << ", "
                                                   << mxb_strerror(errno));
        }
        else
        {
            MXB_SINFO("Start decompressing " << m_input_file << " to " << m_temp_file.name());
            m_output_file = m_temp_file.name();
            m_sDecompressor = std::make_unique<maxbase::Decompressor>();
            m_future = std::async(std::launch::async,
                                  &maxbase::Decompressor::decompress,
                                  &*m_sDecompressor,
                                  std::ref(m_compressed_in),
                                  std::ref(m_decompressed_out));
        }
    }
    else
    {
        m_output_file = m_input_file;
    }
}

BinlogFile::~BinlogFile()
{
    if (m_local_stat == DECOMPRESSING)
    {
        try
        {
            if (m_sDecompressor)
            {
                m_sDecompressor->stop();
            }

            m_future.get();
        }
        catch (std::exception& ex)
        {
            MXB_SERROR("Decompressor threw in ~BinlogFile, input file '"
                       << m_input_file << "' ex: " << ex.what());
        }
    }
}

BinlogFile::OpenRes BinlogFile::open_file(std::string_view file_name_)
{
    auto file_name = std::string(file_name_);
    strip_extension(file_name, COMPRESSION_EXTENSION);
    std::ifstream file(file_name);
    if (file.good())
    {
        return {REGULAR_FILE, file_name, std::move(file)};
    }

    file_name += '.' + COMPRESSION_EXTENSION;
    file.open(file_name);

    if (file.good())
    {
        return {DECOMPRESSING, std::move(file_name), std::move(file)};
    }
    else
    {
        return {FAILED, std::move(file_name), std::move(file)};
    }
}

bool BinlogFile::check_status()
{
    if (m_local_stat == DECOMPRESSING && m_future.wait_for(0s) == std::future_status::ready)
    {
        if (auto stat = m_future.get(); stat != maxbase::CompressionStatus::OK)
        {
            MXB_THROW(BinlogReadError, "Decompression error: "
                      << to_string(stat)
                      << " from file '" << m_input_file << '\'');
        }
        m_local_stat = DECOMPRESS_DONE;
        MXB_SINFO("Decompression done " << m_input_file << " to " << m_temp_file.name());
    }

    return m_local_stat == DECOMPRESSING;
}
}
