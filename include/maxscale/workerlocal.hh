/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
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
#include <type_traits>

namespace maxscale
{

template<class T>
struct WLConstructor
{
    static void delete_value(void* data)
    {
        delete static_cast<T*>(data);
    }

    static size_t sizeof_value(void* data)
    {
        return sizeof(T);
    }
};

template<class T>
struct WLDefaultConstructor : public WLConstructor<T>
{
    T* operator()(const T& t)
    {
        return new T;
    }
};

template<class T>
struct WLCopyConstructor : public WLConstructor<T>
{
    T* operator()(const T& t)
    {
        return new T(t);
    }
};

// Data local to a routing worker
template<class T, class TypeConstructor = WLCopyConstructor<T>>
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
        mxs::RoutingWorker::broadcast([key = m_handle]() {
            mxs::RoutingWorker::get_current()->storage().delete_data(key);
        }, mxs::RoutingWorker::EXECUTE_AUTO);
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
     * Collect local values from all active RoutingWorkers
     *
     * @note: This method must only be called from the MainWorker.
     *
     * @return A vector containing the individual values for each routing worker
     */
    std::vector<T> collect_values() const
    {
        mxb_assert_message(MainWorker::is_current() || mxs::test::is_test(),
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

        if (MainWorker::is_current())
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

            auto deleter = &TypeConstructor::delete_value;
            auto sizer = &TypeConstructor::sizeof_value;

            storage->set_data(m_handle, my_value, deleter, sizer);
        }

        mxb_assert(my_value);
        return my_value;
    }
};

/**
 * A class that provides (mostly) lock-free reads of a globally shared variable.
 *
 * The intended use-case of this class is to provide a convenient way of accessing global data without having
 * to use a mutex to protect the access. The idea use-case for this is any large object that is infrequently
 * updated (e.g. configuration data) but used relatively often from a RoutingWorker.
 *
 * The variable is wrapped in a std::shared_ptr which means there's at most one copy of the value at any given
 * time. The class provides const access to the worker-local value. All updates to the value must be done
 * using the assign() function.
 *
 * The access to the worker-local value only blocks for the first time that the value is accessed. This
 * happens as the local reference is copied from the global one. This may happen long after the WorkerGlobal
 * has been created if the number of threads in MaxScale increases. The update of the value is done under a
 * lock which means no further synchronization is needed when the values are read.
 *
 * Currently the assign() function uses mxs::RoutingWorker::execute_concurrently() which means that all
 * updates must be done in the MainWorker. This prevents multiple pending updates to the value and guarantees
 * that once an update is done, all new sessions in MaxScale will see the value.
 */
template<class T, typename Stored = std::add_const_t<T>>
class WorkerGlobal : private WorkerLocal<std::shared_ptr<Stored>>
{
public:
    // Forwarding constructor
    template<typename ... Args>
    WorkerGlobal(Args&& ... args)
        : WorkerLocal<std::shared_ptr<Stored>>(std::make_shared<Stored>(std::forward<Args>(args)...))
    {
    }

    // Arrow operator
    Stored* operator->() const
    {
        return this->get_local_value()->get();
    }

    // Const version of dereference operator
    Stored& operator*() const
    {
        return *this->get_local_value()->get();
    }

    /**
     * Get a reference to the underlying value
     *
     * This can be useful for "freezing" configuration objects at a given state so that future modifications
     * will not affect it.
     *
     * @return A reference to the current value
     */
    std::shared_ptr<Stored> get_ref() const
    {
        return *this->get_local_value();
    }

    /**
     * Get a reference to the master value.
     */
    std::shared_ptr<Stored> get_master_ref() const
    {
        // Could be called from RoutingWorker, but generally should not be.
        mxb_assert(mxs::RoutingWorker::get_current() == nullptr);
        std::lock_guard<std::mutex> guard(this->m_lock);
        return this->m_value;
    }

    /**
     * Assign a value from an existing reference
     *
     * Sets the master value and triggers an update on all routing workers.
     * The value will be updated on all routing worker threads once the
     * function returns.
     *
     * @note: This function cannot be called from a RoutingWorker
     *
     * @param t The new value to assign
     */
    void assign(std::shared_ptr<Stored> new_val)
    {
        mxb_assert_message(mxs::RoutingWorker::get_current() == nullptr,
                           "this method cannot be called from a RoutingWorker thread");

        // Update the value of the master copy
        std::unique_lock<std::mutex> guard(this->m_lock);
        this->m_value = std::move(new_val);
        guard.unlock();

        mxs::MainWorker::get()->execute([this] {
            update_local_value();
        }, mxs::MainWorker::EXECUTE_AUTO);

        // Update the local value of all workers from the master copy
        mxs::RoutingWorker::execute_concurrently(
            [this]() {
            update_local_value();
        });
    }

    /**
     * Assign a value
     *
     * Sets the master value and triggers an update on all routing workers.
     * The value will be updated on all routing worker threads once the
     * function returns.
     *
     * @note: This function cannot be called from a RoutingWorker
     *
     * @param t The new value to assign
     */
    void assign(Stored& t)
    {
        assign(std::make_shared<Stored>(t));
    }

private:

    void update_local_value()
    {
        // As get_local_value can cause a lock to be taken, we need the pointer to our value before
        // we lock the master value for the updating of our value.
        auto* my_value = this->get_local_value();

        std::lock_guard<std::mutex> guard(this->m_lock);
        *my_value = this->m_value;
    }
};
}
