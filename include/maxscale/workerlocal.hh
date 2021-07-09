/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/indexedstorage.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/test.hh>

namespace maxscale
{

/**
 * Deletes local data from all workers
 *
 * The key must not be used again after deletion.
 *
 * @param key      Key to remove
 */
void worker_local_delete_data(uint64_t key);

template<class T>
struct DefaultConstructor
{
    T* operator()(const T& t)
    {
        return new T;
    }
};

template<class T>
struct CopyConstructor
{
    T* operator()(const T& t)
    {
        return new T(t);
    }
};

// Data local to a routing worker
template<class T, class TypeConstructor = CopyConstructor<T>>
class WorkerLocal
{
public:

    WorkerLocal(const WorkerLocal&) = delete;
    WorkerLocal& operator=(const WorkerLocal&) = delete;

    // Default initialized
    WorkerLocal()
        : m_handle(IndexedStorage::create_key())
    {
    }

    // Forwarding constructor
    template<typename ... Args>
    WorkerLocal(Args&& ... args)
        : m_handle(IndexedStorage::create_key())
        , m_value(std::forward<Args>(args)...)
    {
    }

    ~WorkerLocal()
    {
        worker_local_delete_data(m_handle);
    }

    // Converts to a T reference
    operator T&() const
    {
        return *get_local_value();
    }

    // Arrow operator
    T* operator->() const
    {
        return get_local_value();
    }

    // Dereference operator
    T& operator*()
    {
        return *get_local_value();
    }

    // Const version of dereference operator
    const T& operator*() const
    {
        return *get_local_value();
    }

protected:

    uint64_t                            m_handle;   // The handle to the worker local data
    typename std::remove_const<T>::type m_value;    // The master value, never used directly
    mutable std::mutex                  m_lock;     // Protects the master value

    /**
     * Get the local value
     *
     * @note: This method must only be called from the MainWorker or a routing worker.
     */
    T* get_local_value() const
    {
        IndexedStorage* storage = nullptr;

        if (MainWorker::is_main_worker())
        {
            storage = &MainWorker::get()->storage();
        }
        else
        {
            auto worker = RoutingWorker::get_current();
            mxb_assert(worker);
            storage = &worker->storage();
        }

        T* my_value = static_cast<T*>(storage->get_data(m_handle));

        if (my_value == nullptr)
        {
            // First time we get the local value, allocate it from the master value
            std::unique_lock<std::mutex> guard(m_lock);
            my_value = TypeConstructor()(m_value);
            guard.unlock();

            storage->set_data(m_handle, my_value, destroy_value);
        }

        mxb_assert(my_value);
        return my_value;
    }

    static void destroy_value(void* data)
    {
        delete static_cast<T*>(data);
    }
};

// Extends WorkerLocal with global read and write methods for updating all stored values.
template<class T>
class WorkerGlobal : public WorkerLocal<T>
{
public:
    using WorkerLocal<T>::WorkerLocal;
    // CentOS 7 fails to build if an alias is used to access base class methods instead of the this pointer
    // using Base = WorkerLocal<T>;

    /**
     * Assign a value
     *
     * Sets the master value and triggers an update on all routing workers.
     * The value will be updated on all routing worker threads once the
     * function returns.
     *
     * @note: This function must only be called from the MainWorker.
     *
     * @param t The new value to assign
     */
    void assign(const T& t)
    {
        mxb_assert_message(MainWorker::is_main_worker() || mxs::test::is_test(),
                           "this method must be called from the main worker thread");

        // Update the value of the master copy
        std::unique_lock<std::mutex> guard(this->m_lock);
        this->m_value = t;
        guard.unlock();

        update_local_value();

        // Update the local value of all workers from the master copy
        mxs::RoutingWorker::execute_concurrently(
            [this]() {
                update_local_value();
            });
    }

    /**
     * Get all local values
     *
     * @note: This method must only be called from the MainWorker.
     *
     * @return A vector containing the individual values for each routing worker
     */
    std::vector<T> values() const
    {
        mxb_assert_message(MainWorker::is_main_worker() || mxs::test::is_test(),
                           "this method must be called from the main worker thread");
        std::vector<T> rval;
        std::mutex lock;

        mxs::RoutingWorker::execute_concurrently(
            [&]() {
                std::lock_guard<std::mutex> guard(lock);
                rval.push_back(*this->get_local_value());
            });

        return rval;
    }

private:

    void update_local_value()
    {
        // As get_local_value can cause a lock to be taken, we need the pointer to our value before
        // we lock the master value for the updating of our value.
        T* my_value = this->get_local_value();

        std::lock_guard<std::mutex> guard(this->m_lock);
        *my_value = this->m_value;
    }
};
}
