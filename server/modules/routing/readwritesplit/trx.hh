#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <deque>

#include <maxscale/buffer.hh>
#include <maxscale/utils.hh>

// A transaction
class Trx
{
public:
    // A log of executed queries, for transaction replay
    typedef std::deque<mxs::Buffer> TrxLog;

    Trx():
        m_size(0)
    {
    }

    /**
     * Add a statement to the transaction
     *
     * @param buf Statement to add
     */
    void add_stmt(GWBUF* buf)
    {
        m_size += gwbuf_length(buf);
        m_log.push_back(buf);
    }

    /**
     * Add a result to the transaction
     *
     * The result is used to update the checksum.
     *
     * @param buf Result to add
     */
    void add_result(GWBUF* buf)
    {
        m_checksum.update(buf);
    }

    /**
     * Releases the oldest statement in this transaction
     *
     * This reduces the size of the transaction by one and should only be used
     * to replay a transaction.
     *
     * @return The oldest statement in this transaction
     */
    GWBUF* pop_stmt()
    {
        ss_dassert(!m_log.empty());
        GWBUF* rval = m_log.front().release();
        m_log.pop_front();
        return rval;
    }

    /**
     * Finalize the transaction
     *
     * This function marks the transaction as completed be that by a COMMIT
     * or by a failure of the current server where the transaction was being
     * executed.
     */
    void finalize()
    {
        m_checksum.finalize();
    }

    /**
     * Check if transaction is empty
     *
     * @return True if transaction has no statements
     */
    bool empty() const
    {
        return m_log.empty();
    }

    /**
     * Get transaction size in bytes
     *
     * @return Size of the transaction in bytes
     */
    size_t size() const
    {
        return m_size;
    }

    /**
     * Close the transaction
     *
     * This clears out the stored statements and resets the checksum state.
     */
    void close()
    {
        m_checksum.reset();
        m_log.clear();
        m_size = 0;
    }

    /**
     * Return the current checksum
     *
     * finalize() must be called before the return value of this function is used.
     *
     * @return The checksum of the transaction
     */
    const mxs::SHA1Checksum& checksum() const
    {
        return m_checksum;
    }

private:
    mxs::SHA1Checksum m_checksum; /**< Checksum of the transaction */
    TrxLog            m_log; /**< The transaction contents */
    size_t            m_size; /**< Transaction size in bytes */
};
