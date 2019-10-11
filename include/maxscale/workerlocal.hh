/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
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

// Data local to a routing worker
template<class T>
class rworker_local
{
public:

    rworker_local(const rworker_local&) = delete;
    rworker_local& operator=(const rworker_local&) = delete;

    // Default initialized
    rworker_local()
        : m_handle(IndexedStorage::create_key())
    {
    }

    // Forwarding constructor
    template<typename ... Args>
    rworker_local(Args&& ... args)
        : m_handle(IndexedStorage::create_key())
        , m_value(std::forward<Args>(args)...)
    {
    }

    ~rworker_local()
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
        mxb_assert_message(MainWorker::is_main_worker(),
                           "this method must be called from the main worker thread");

        // Update the value of the master copy
        std::unique_lock<std::mutex> guard(m_lock);
        m_value = t;
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
        mxb_assert_message(MainWorker::is_main_worker(),
                           "this method must be called from the main worker thread");
        std::vector<T> rval;
        std::mutex lock;

        mxs::RoutingWorker::execute_concurrently(
            [&]() {
                std::lock_guard<std::mutex> guard(lock);
                rval.push_back(*get_local_value());
            });

        return rval;
    }

private:

    uint64_t                            m_handle;   // The handle to the worker local data
    typename std::remove_const<T>::type m_value;    // The master value, never used directly
    mutable std::mutex                  m_lock;     // Protects the master value

private:
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
            my_value = new T(m_value);
            guard.unlock();

            storage->set_data(m_handle, my_value, destroy_value);
        }

        mxb_assert(my_value);
        return my_value;
    }

    void update_local_value()
    {
        // As get_local_value can cause a lock to be taken, we need the pointer to our value before
        // we lock the master value for the updating of our value.
        T* my_value = get_local_value();

        std::lock_guard<std::mutex> guard(m_lock);
        *my_value = m_value;
    }

    static void destroy_value(void* data)
    {
        delete static_cast<T*>(data);
    }
};

}
