#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/spinlock.h>

namespace maxscale
{

class SpinLockGuard;

/**
 * @class SpinLock spinlock.hh <maxscale/spinlock.hh>
 *
 * The class SpinLock is a simple class that wraps a regular SPINLOCK
 * and provides equivalent functionality.
 */
class SpinLock
{
public:
    friend class SpinLockGuard;

    /**
     * Creates a spinlock.
     */
    SpinLock()
    {
        spinlock_init(&m_lock);
    }

    ~SpinLock()
    {
    }

    /**
     * Acquires the spinlock.
     */
    void acquire()
    {
        spinlock_acquire(&m_lock);
    }

    /**
     * Releases the spinlock.
     */
    void release()
    {
        spinlock_release(&m_lock);
    }

private:
    SpinLock(const SpinLock&) /* = delete */;
    SpinLock& operator = (const SpinLock&) /* = delete */;

private:
    SPINLOCK m_lock;
};

/**
 * @class SpinLockGuard spinlock.hh <maxscale/spinlock.hh>
 *
 * The class SpinLockGuard is a spinlock wrapper that provides a RAII-style
 * mechanism for owning a spinlock for the duration of a scoped block. When
 * a SpinLockGuard object is created, it attempts to take ownership of the
 * spinlock it is given. When control leaves the scope in which the
 * SpinLockGuard object was created, the SpinLockGuard is destructed and the
 * mutex is released.
 */
class SpinLockGuard
{
public:
    /**
     * Creates the guard and locks the provided lock.
     *
     * @param lock  The spinlock to lock.
     */
    SpinLockGuard(SPINLOCK& lock)
        : m_lock(lock)
    {
        spinlock_acquire(&m_lock);
    }

    /**
     * Creates the guard and locks the provided lock.
     *
     * @param lock  The spinlock to lock.
     */
    SpinLockGuard(SpinLock& lock)
        : m_lock(lock.m_lock)
    {
        spinlock_acquire(&m_lock);
    }

    /**
     * Destroys the guard and unlocks the lock that was provided when
     * the guard was created.
     */
    ~SpinLockGuard()
    {
        spinlock_release(&m_lock);
    }

private:
    SpinLockGuard(const SpinLockGuard&) /* = delete */;
    SpinLockGuard& operator = (const SpinLockGuard&) /* = delete */;

    SPINLOCK& m_lock;
};

}
