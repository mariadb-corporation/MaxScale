/*
 * Copyright (c) 2023 MariaDB plc
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

#include <maxbase/compress.hh>
#include <maxbase/log.hh>
#include <maxbase/assert.hh>
#include <maxbase/string.hh>
#include <sstream>
#include <zstd.h>
#include <zstd_errors.h>
#include <thread>
#include <algorithm>

// TODO verify_integrity
// TODO cpu limit
// TODO don't compile if zstd not available,
//      but add libzstd to install_build_deps.sh


namespace maxbase
{
Compressor::Compressor(int level, int nthreads, float cpu_limit)
    : m_pContext(ZSTD_createCCtx())
    , m_input_buffer(ZSTD_CStreamInSize())
    , m_output_buffer(ZSTD_CStreamOutSize())
    , m_level(level)
    , m_nthreads(nthreads)
{
    set_cpu_limit(cpu_limit);

    if (m_nthreads == -1)
    {
        m_nthreads = std::thread::hardware_concurrency();
    }

    if (m_pContext == nullptr)
    {
        m_status = CompressionStatus::INIT_ERROR;
        MXB_SERROR("Failed to create Compressor context");
    }
}

Compressor::~Compressor()
{
    ZSTD_freeCCtx(m_pContext);
}

size_t Compressor::last_comp_error() const
{
    return m_last_err;
}

std::string Compressor::last_comp_error_str() const
{
    return ZSTD_getErrorName(m_last_err);
}

void Compressor::set_level(int level)
{
    m_level = level;
}

void Compressor::set_nthread(int nthreads)
{
    m_nthreads = nthreads;
}

float Compressor::set_cpu_limit(float percent)
{
    return m_cpu_limit = std::clamp(percent, 0.25f, 1.0f);
}

void Compressor::set_buffer_sizes(size_t input_size, size_t output_size)
{
    m_input_buffer.resize(input_size);
    m_output_buffer.resize(output_size);
}

CompressionStatus Compressor::compress(std::istream& in,
                                       std::ostream& out)
{
    mxb_assert(m_pContext != nullptr);

    if (m_last_err != ZSTD_error_no_error)
    {
        m_last_err = ZSTD_CCtx_reset(m_pContext, ZSTD_reset_session_only);

        if (ZSTD_isError(m_last_err))
        {
            MXB_SERROR("Failed to reset compressor context: " << last_comp_error_str());
            m_status = CompressionStatus::COMPRESSION_ERROR;
            return m_status;
        }
    }

    m_last_err = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_checksumFlag, 1);

    if (!ZSTD_isError(m_last_err))
    {
        m_last_err = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_compressionLevel, m_level);
    }

    if (ZSTD_isError(m_last_err))
    {
        MXB_SERROR("Failed to set compressor parameter: " << last_comp_error_str());
        m_status = CompressionStatus::COMPRESSION_ERROR;
        return m_status;
    }

    if (m_nthreads > 1)
    {
        size_t const ret = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_nbWorkers, m_nthreads);

        if (ZSTD_isError(ret))
        {
            MXB_SINFO("Installed libzstd does not support multithreading."
                      " Continue single threaded execution.");
        }
    }

    long to_read = m_input_buffer.size();
    auto has_input = false;

    do
    {
        in.read(m_input_buffer.data(), to_read);
        if (in.fail() && !in.eof())
        {
            if (in.bad())
            {
                m_status = CompressionStatus::IO_ERROR;
                MXB_SERROR("IO error reading input stream");
            }
            else
            {
                m_status = CompressionStatus::IO_ERROR;
                MXB_SERROR("Failed to read input stream");
            }
        }

        has_input = has_input || in.gcount() > 0;

        bool last_chunk = in.gcount() < to_read;
        ZSTD_EndDirective mode = last_chunk ? ZSTD_e_end : ZSTD_e_continue;
        ZSTD_inBuffer input{m_input_buffer.data(), size_t(in.gcount()), 0};

        auto done = false;
        do
        {
            ZSTD_outBuffer output = {m_output_buffer.data(), m_output_buffer.size(), 0};
            // The return value is either remaining bytes to be flushed, or
            // an error code.
            m_last_err = ZSTD_compressStream2(m_pContext, &output, &input, mode);

            if (ZSTD_isError(m_last_err))
            {
                MXB_SERROR("Compression error: " << last_comp_error_str());
                m_status = CompressionStatus::COMPRESSION_ERROR;
                return m_status;
            }

            out.write(m_output_buffer.data(), output.pos);
            if (out.fail())
            {
                if (out.bad())
                {
                    m_status = CompressionStatus::IO_ERROR;
                    MXB_SERROR("IO error writing to output stream");
                }
                else
                {
                    m_status = CompressionStatus::IO_ERROR;
                    MXB_SERROR("Failed to write to output stream");
                }
            }

            done = last_chunk ? m_last_err == 0 : input.pos == input.size;
        }
        while (!done);
    }
    while (in.gcount() != 0);

    out.flush();

    if (!has_input)
    {
        mxb_assert(!true);
        MXB_SERROR("Empty input stream");
        m_status = CompressionStatus::EMPTY_INPUT_STREAM;
    }
    else
    {
        m_status = CompressionStatus::OK;
    }

    return m_status;
}

CompressionStatus Compressor::status() const
{
    return m_status;
}

bool Compressor::verify_integrity(const std::istream& in)
{
    // TODO, what is the fastest method.
    return false;
}

int Compressor::level() const
{
    return m_level;
}

int Compressor::ntheads() const
{
    return m_nthreads;
}

int Compressor::cpu_limit() const
{
    return m_cpu_limit;
}

Decompressor::Decompressor(int flush_nchars)
    : m_pContext(ZSTD_createDCtx())
    , m_input_buffer(ZSTD_CStreamInSize())
    , m_output_buffer(ZSTD_CStreamOutSize())
    , m_flush_nchars(flush_nchars)
{
    if (m_pContext == nullptr)
    {
        m_status = CompressionStatus::INIT_ERROR;
        MXB_SERROR("Could not create decompressor context.");
    }
}

maxbase::Decompressor::~Decompressor()
{
    ZSTD_freeDCtx(m_pContext);
}

CompressionStatus Decompressor::status() const
{
    return m_status;
}

CompressionStatus Decompressor::decompress(std::istream& in,
                                           std::ostream& out)
{
    mxb_assert(m_pContext != nullptr);

    if (m_last_err != ZSTD_error_no_error)
    {
        m_last_err = ZSTD_DCtx_reset(m_pContext, ZSTD_reset_session_only);

        if (ZSTD_isError(m_last_err))
        {
            MXB_SERROR("Failed to reset compressor context: " << last_comp_error_str());
            m_status = CompressionStatus::COMPRESSION_ERROR;
            return m_status;
        }
    }

    auto to_read = m_input_buffer.size();
    auto no_input = true;
    size_t bytes_out = 0;
    size_t last_ret;

    for (;;)
    {
        in.read(m_input_buffer.data(), to_read);
        if (in.fail() && !in.eof())
        {
            if (in.bad())
            {
                m_status = CompressionStatus::IO_ERROR;
                MXB_SERROR("IO error reading input stream: " << mxb_strerror(errno));
            }
            else
            {
                m_status = CompressionStatus::IO_ERROR;
                MXB_SERROR("Failed to read input stream: " << mxb_strerror(errno));
                mxb_assert(!true);
            }
        }

        if (in.gcount() == 0)
        {
            break;
        }

        no_input = false;

        ZSTD_inBuffer input{m_input_buffer.data(), size_t(in.gcount()), 0};
        while (m_stop.load(std::memory_order_relaxed) == false && input.pos < input.size)
        {
            ZSTD_outBuffer output = {m_output_buffer.data(), m_output_buffer.size(), 0};

            // The return value is either remaining bytes to be flushed, or
            // an error code.
            last_ret = ZSTD_decompressStream(m_pContext, &output, &input);
            m_last_err = last_ret;

            if (ZSTD_isError(m_last_err))
            {
                MXB_SERROR("Decompression error = " << ZSTD_getErrorName(m_last_err));
                m_status = CompressionStatus::COMPRESSION_ERROR;
                return m_status;
            }

            out.write(m_output_buffer.data(), output.pos);
            if (out.fail())
            {
                if (out.bad())
                {
                    m_status = CompressionStatus::IO_ERROR;
                    MXB_SERROR("IO error reading input stream: " << mxb_strerror(errno));
                }
                else
                {
                    m_status = CompressionStatus::IO_ERROR;
                    MXB_SERROR("Failed to read input stream: " << mxb_strerror(errno));
                    mxb_assert(!true);
                }
            }

            bytes_out += output.pos;
            if (m_flush_nchars && bytes_out > m_flush_nchars)
            {
                out.flush();
                bytes_out = 0;
            }
        }
    }

    if (m_stop.load(std::memory_order_relaxed))
    {
        return m_status;
    }

    out.flush();

    if (no_input)
    {
        mxb_assert(!true);
        MXB_SERROR("Empty input stream");
        m_status = CompressionStatus::EMPTY_INPUT_STREAM;
    }
    else if (last_ret)
    {
        MXB_SERROR("Decompression error, possible truncated stream.");
        m_status = CompressionStatus::COMPRESSION_ERROR;
    }
    else
    {
        m_status = CompressionStatus::OK;
    }

    return m_status;
}

void Decompressor::stop()
{
    m_stop.store(true, std::memory_order_relaxed);
}

size_t Decompressor::last_comp_error() const
{
    return m_last_err;
}

std::string Decompressor::last_comp_error_str() const
{
    return ZSTD_getErrorName(m_last_err);
}

void Decompressor::set_buffer_sizes(size_t input_size, size_t output_size)
{
    m_input_buffer.resize(input_size);
    m_output_buffer.resize(output_size);
}

std::string to_string(CompressionStatus status)
{
    switch (status)
    {
    case CompressionStatus::OK:
        return "OK";

    case CompressionStatus::COMPRESSION_ERROR:
        return "COMPRESSION_ERROR";

    case CompressionStatus::EMPTY_INPUT_STREAM:
        return "EMPTY_INPUT_STREAM";

    case CompressionStatus::INIT_ERROR:
        return "INIT_ERROR";

    case CompressionStatus::IO_ERROR:
        return "IO_ERROR";
    }

    mxb_assert(!true);
    return "UNKNOWN";
}
}
