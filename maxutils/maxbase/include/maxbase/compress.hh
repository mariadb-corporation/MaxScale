/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <iostream>
#include <memory>
#include <vector>
#include <atomic>

class ZSTD_CCtx_s;
class ZSTD_DCtx_s;

namespace maxbase
{

/** Compression algorithm */
enum class CompressionAlgorithm
{
    NONE,
    ZSTANDARD
};

/*
 *  The status INIT_ERROR is set if Compressor or Decompressor
 *  constructors fail to initialize the compression library. When the status
 *  is COMPRESSION_ERROR last_comp_error() and last_comp_error_str()
 *  return the compression library specific error.
 */
enum class CompressionStatus : char
{
    OK,
    COMPRESSION_ERROR,
    EMPTY_INPUT_STREAM,
    INIT_ERROR,
    IO_ERROR
};

std::string to_string(CompressionStatus status);

/**
 * @brief Compressor class. One instance per thread, decompress can be
 *        called multiple times on the same instance (avoid re-creation of
 *        stream context).
 */
class Compressor final
{
public:
    /**
     * @brief Compressor   Check the status for INIT_ERROR after creation.
     * @param level        - Compress level 0-19, in practice 2-6 give reasonable
     *                       compression ratio/speed.
     * @param nthreads     - How many threads compression uses internally. The value -1
     *                       causes all available hardware threads to be used.
     * @param cpu_limit    - Limit CPU usage: 1.0 no limit, min 0.25. This does not
     *                       affect the speeed at which the compression functions operate,
     *                       but the CPU usage of the calling thread.
     */
    Compressor(int level, int nthreads = -1, float cpu_limit = 0);
    Compressor(Compressor&&) = delete;
    ~Compressor();

    /**
     * @brief  If the status() is COMPRESSION_ERROR, last_comp_error() and last_comp_error_str()
     *         report the underlying compression library error.
     * @return CompressionStatus
     */
    CompressionStatus status() const;

    /**
     * @brief compress
     * @param in            - Stream in  is read until no more bytes are available,
     *                        so is not suitable for a stream that is being written to
     *                        while compression is in process.
     * @param out           - Stream out
     * @return CompressionStatus
     */
    CompressionStatus compress(std::istream& in, std::ostream& out);

    /**
     * @brief verify_integrity
     * @param in    - Stream in
     * @return bool - true if stream is a valid compressed file
     */
    static bool verify_integrity(const std::istream& in);

    /**
     * @brief  last_comp_error - Last compression library specific error. The error
     *                           is reset when decompress() is called.
     * @return compression library specfic error code, 0 is no error
     */
    size_t last_comp_error() const;

    /**
     * @brief last_error_str
     * @return human readable error string
     */
    std::string last_comp_error_str() const;

    // Getters mainly for loggin
    int level() const;
    int ntheads() const;
    int cpu_limit() const;

    // For tuning and testing.
    void  set_level(int level);
    void  set_nthread(int nthreads);
    float set_cpu_limit(float limit);
    void  set_buffer_sizes(size_t input_size, size_t output_size);

private:
    ZSTD_CCtx_s*      m_pContext;
    std::vector<char> m_input_buffer;
    std::vector<char> m_output_buffer;
    CompressionStatus m_status = CompressionStatus::OK;
    size_t            m_last_err = 0;
    int               m_level;
    int               m_nthreads;
    float             m_cpu_limit = 0;
};

class Decompressor final
{
public:
    /**
     * @brief Decompressor    Check the status for INIT_ERROR after creation.
     *        called multiple times on the same instance (avoid re-creation of
     *        stream context).
     *
     * @param flush_nchars - flush stream every n chars, 0 means flush only at the end.
     */
    Decompressor(int flush_nchars = 0);
    Decompressor(Decompressor&&) = delete;
    ~Decompressor();

    /**
     * @brief  status - If the status() is COMPRESSION_ERROR, last_comp_error()
     *                  and last_comp_error_str() report the underlying
     *                  compression library error.
     * @return CompressionStatus
     */
    CompressionStatus status() const;

    /**
     * @brief decompress
     * @param in            - Stream in is read until no more bytes are available,
     *                        so is not suitable for a stream that is being written to
     *                        while compression is in process.
     * @param out           - Stream out. If out is read while decompression
     * @return CompressionStatus
     */
    CompressionStatus decompress(std::istream& in, std::ostream& out);

    /**
     * @brief stop - Stops the decompression loop. The status remains what it
     *               was when the last iteration ends. This is useful when decompress()
     *               is being run in a separate thread by the client, but decompression
     *               does not need to continue to run for the full file.
     */
    void stop();

    /**
     * @brief  last_comp_error - Last compression library specific error. The error
     *                           is reset when decompress() is called.
     * @return compression library specfic error code, 0 is no error
     */
    size_t last_comp_error() const;

    /**
     * @brief last_error_str
     * @return human readable error string
     */
    std::string last_comp_error_str() const;

    // For tuning and testing.
    void set_buffer_sizes(size_t input_size, size_t output_size);

private:
    ZSTD_DCtx_s*      m_pContext;
    std::vector<char> m_input_buffer;
    std::vector<char> m_output_buffer;
    size_t            m_flush_nchars;
    size_t            m_last_err = 0;
    CompressionStatus m_status = CompressionStatus::OK;
    std::atomic<bool> m_stop{false};
};
}
