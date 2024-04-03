/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include <maxscale/ccdefs.hh>

#include <maxbase/checksum.hh>
#include <maxscale/target.hh>

// A statement in a transaction.
struct Stmt
{
    using Checksum = mxb::xxHash;

    GWBUF    buffer;
    Checksum checksum;
    size_t   bytes {0};

    Stmt shallow_clone()
    {
        return {buffer.shallow_clone(), checksum, bytes};
    }

    void clear()
    {
        buffer.clear();
        checksum.reset();
        bytes = 0;
    }

    operator bool() const
    {
        return !!buffer;
    }

    bool empty() const
    {
        return buffer.empty();
    }
};

// A transaction consisting of statements
class Trx
{
public:
    // A log of executed queries, for transaction replay
    typedef std::list<GWBUF>                        TrxLog;
    typedef std::vector<Stmt::Checksum::value_type> ChecksumLog;

    Trx()
        : m_size(0)
        , m_target(nullptr)
    {
    }

    Trx(const Trx& rhs)
    {
        m_checksums = rhs.m_checksums;
        m_size = rhs.m_size;
        m_target = rhs.m_target;

        m_log.clear();

        for (const auto& buffer : rhs.m_log)
        {
            m_log.emplace_back(buffer.shallow_clone());
        }
    }

    Trx& operator=(const Trx& rhs)
    {
        if (this != &rhs)
        {
            m_checksums = rhs.m_checksums;
            m_size = rhs.m_size;
            m_target = rhs.m_target;

            m_log.clear();

            for (const auto& buffer : rhs.m_log)
            {
                m_log.emplace_back(buffer.shallow_clone());
            }
        }

        return *this;
    }

    mxs::RWBackend* target() const
    {
        return m_target;
    }

    void set_target(mxs::RWBackend* tgt)
    {
        m_target = tgt;
    }

    /**
     * Add a statement to the transaction
     *
     * @param buf Statement to add
     */
    void add_stmt(mxs::RWBackend* target, GWBUF&& buf)
    {
        mxb_assert_message(buf, "Trx::add_stmt: Buffer must not be empty");
        mxb_assert(target);

        m_size += buf.runtime_size();
        m_log.emplace_back(std::move(buf));

        mxb_assert_message(target == m_target, "Target should be '%s', not '%s'",
                           m_target ? m_target->name() : "<no target>", target->name());
    }

    /**
     * Add a result to the transaction
     *
     * The result is used to update the checksum.
     *
     * @param buf Result to add
     */
    void add_result(const Stmt::Checksum::value_type& checksum)
    {
        m_checksums.push_back(checksum);
    }

    /**
     * Releases the oldest statement in this transaction
     *
     * This reduces the size of the transaction by one and should only be used
     * to replay a transaction.
     *
     * @return The oldest statement in this transaction
     */
    GWBUF pop_stmt()
    {
        mxb_assert(!m_log.empty());
        GWBUF rval = std::move(m_log.front());
        m_log.pop_front();
        return rval;
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
        m_checksums.clear();
        m_log.clear();
        m_size = 0;
        m_target = nullptr;
    }

    /**
     * @return The checksums of the transaction
     */
    const ChecksumLog& checksums() const
    {
        return m_checksums;
    }

private:
    ChecksumLog     m_checksums;/**< Checksum of the transaction */
    TrxLog          m_log;      /**< The transaction contents */
    size_t          m_size;     /**< Transaction size in bytes */
    mxs::RWBackend* m_target;   /**< The target on which the transaction is done */
};
