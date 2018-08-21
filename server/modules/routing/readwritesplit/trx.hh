/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
 #pragma once

#include <maxscale/ccdefs.hh>

#include <list>

#include <maxscale/buffer.hh>
#include <maxscale/utils.hh>
#include <maxscale/modutil.hh>

// A transaction
class Trx
{
public:
    // A log of executed queries, for transaction replay
    typedef std::list<mxs::Buffer> TrxLog;

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
        ss_info_dassert(buf, "Trx::add_stmt: Buffer must not be empty");

        if (mxs_log_is_priority_enabled(LOG_INFO))
        {
            MXS_INFO("Adding to trx: %s", mxs::extract_sql(buf, 512).c_str());
        }

        m_size += gwbuf_length(buf);
        m_log.emplace_back(buf);
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
     * Check if transaction has statements
     *
     * @return True if transaction has statements
     *
     * @note This function should only be used when checking whether a transaction
     *       that is being replayed has ended. The empty() method can be used
     *       to check whether statements were added to the transaction.
     */
    bool have_stmts() const
    {
        return !m_log.empty();
    }

    /**
     * Check whether the transaction is empty
     *
     * @return True if no statements have been added to the transaction
     */
    bool empty() const
    {
        return m_size == 0;
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
