/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/assert.hh>
#include <maxbase/stopwatch.hh>

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace maxbase
{

/// Move CachelineAlignment and CachelineAtomic into something more appropriate TODO
constexpr int CachelineAlignment = 64;

template<typename T>
struct alignas (CachelineAlignment) CachelineAtomic : public std::atomic<T>
{
    using std::atomic<T>::atomic;
};

/**
 *  @class SharedData
 *
 *  This class represents data shared across multiple threads. Together with a Collector this is
 *  essentially a multiple reader single writer, where updates to the data by the readars are
 *  handled in a writer thread.
 *
 *  Motivation: Make reading shared, cached data a zero overhead operation
 *
 *  Worker - a thread reading data and sending updates
 *  Collector - a thread that reads and processes the updates.
 *
 *  In a multi-core system where shared data that can be modified by any worker, where such data is rarely
 *  modified but constantly read, regular synchronization can come at a heavy cost. One option is for each
 *  thread to manage their own thread local cache, but that can become very costly in terms of memory, or
 *  in performance, if creating a cache entry is a time consuming operation. Also, with thread local caches
 *  and large amounts of data, the cpu caches fill up quicker (L1, L2,...).
 *
 *  If the data is such that updates need not be immediately available, and that even the thread making the
 *  update does not need to be able to immediately read the same data back, SharedData presents a solution.
 *  Usually this kind of shared data is some sort of cache container, but it could be as simple as a single
 *  string with "breaking news" from GNN. It is easy to write specialized Collectors that can perform e.g.
 *  cumulative updates to some data, or, keep track of the 10 most used queries during the past 10 minutes.
 *
 *  SharedData contains two pointers to some data type, the current and the new pointer. Each Reader
 *  has its own copy of SharedData and can always read the current data without any locks by following
 *  the simple protocol of calling Data* ptr = reader_ready() at the top and bottom of their loop.
 *  All that reader_ready() does is to unconditionally copy the new pointer to the current pointer, and
 *  return the current pointer. Whenever capping number of copies of DataType is used (assume always)
 *  reader_ready() should be called at least at the top and bottom of the loop or function that uses
 *  the pointer.
 *  NOTE: Use the RAII SharedDataPtr class to make sure reader_ready() is called properly.
 *
 *  Updates to "data" are handled by a specialized Collector. A worker makes an update using the
 *  ShareDataType::send_update() function. The associated Collector will do what a regular a CAS
 *  reader/writer update would do but in a separate thread, with much more control of the overhead.
 *
 *  The ordering and causality of updates can be handled at various levels of strictness by an instance
 *  of a specialized Collector and some co-operation from the Workers, obviously with a cost to system
 *  performance. However, disregarding causality and read-back, Collector can process updates across all
 *  workers in the order the updates were created. Total ordering is on by default, if it is not needed,
 *  it pays to turn it off. For a thread to be able to immediately read back an update, it would
 *  be possible to add an intermediate cache layer (which could cause causality issues).
 *  At the time of writing, read-back and causality are not issues.
 *
 *  The queue of updates is in the SharedData instance, the instance of which is only shared by one
 *  Worker and one Collector. There are three reasons for this, first and foremost data
 *  locality (low cache-memory pressure), second, it enables the use of an efficient lock-free
 *  single-producer-single-consumer algorithm, third, it scales very well to a large numbers of cores.
 *  (need a reference here, or something magic in the Collector, which WILL become the bottleneck with
 *  lots cores and lots of updates, the latter of which is presumed NOT to happen).
 *
 *  See full example in collector.hh
 *
 *  @tparam Data type being shared. For example std::map<std::string, std::string>.
 *  @tparam Update the type used to send updates. For the std::map case, it could be a struct
 *                 struct MapUpdate
 *                 {
 *                     MapAction action; // enum class MapAction {InsertUpdate, Delete}
 *                     std::string key;
 *                     std::string value;
 *                 };
 *
 */
template<typename Data, typename Update>
class alignas (CachelineAlignment) SharedData
{
public:
    using DataType = Data;
    using UpdateType = Update;

    /**
     * @brief Constructor
     *
     * @param pData the initial DataType instance.
     * @param max_updates max number of updates to queue up. send_update() will block when the queue is full.
     *                    This number should by high enough that send_update() never blocks, or, does not
     *                    block under sensible load conditions.
     */
    SharedData(DataType* pData,
               int max_updates,
               std::condition_variable* Collector_wakeup,
               bool* pData_rdy);

    /**
     * @brief A reader/worker thread should call this each loop to get a ptr to a fresh copy of DataType.
     *        Since the number of copies of DataType* can be capped, it is important that reader_ready() is
     *        called before and after work is done. See SharedDataPtr.
     *
     * @return Pointer to the latest copy of DataType.
     *
     * TODO. When the number of cores increases, at some point "threads = auto" should start less workers
     *       than cores. With 96 cores, save 1-3 cores (really depending on what is expected to run
     *       in non-worker threads).
     */
    const DataType* reader_ready();

    /**
     * @brief A reader/worker calls this function to update DataType. The actual
     *        update will happen at some later time.
     * @param Params suitable to instantiate an UpdateType.
     *        Even if the UpdateType is just a struct, a constructor has
     *        to be written in C++17 but can be omitted in C++20.
     */
    template<typename ...Params>
    void send_update(Params && ... args);

    // For Collector to check if there is buffered data
    // when this SharedData is about to be removed.
    bool has_data() const;

private:
    // Collector is a friend. All private functions are for Collector, and nothing else, to call.
    template<typename Me>
    friend class Collector;

    // For Collector, so it can move SharedData into a vector (in initialization)
    SharedData(SharedData&& rhs) = default;

    void set_new_data(const DataType* pData);
    bool wait_for_updates(maxbase::Duration timeout, std::atomic<bool>* pNo_blocking);
    bool get_updates(std::vector<UpdateType>& swap_me);
    void reset_ptrs();
    void shutdown();

    std::pair<const Data*, const Data*> get_ptrs() const;

    mutable std::mutex       m_ptr_exchange_mutex;
    std::atomic<const Data*> m_pCurrent;
    std::atomic<const Data*> m_pNew;
    std::vector<UpdateType>  m_queue;
    size_t                   m_queue_max;

    std::mutex               m_update_mutex;
    std::condition_variable* m_pCollector_wakeup;
    bool*                    m_pData_rdy;

    std::condition_variable m_worker_wakeup;
    bool                    m_data_swapped_out = false;
};

/**
 *  @class SharedDataPtr
 *
 *  SharedDataPtr is a RAII class to make reader_ready() calls in construction and destruction,
 *  and every pointer access if it is acceptable for the client that the contents
 *  of DataType* may change from call to call. This is acceptable in almost all cases.
 *  - Use SharedDataPtr whenever possible.
 *  - Do not make SharedDataPtr a member of any class that outlives the thread loop,
 *    or the function where DataType* is needed.
 *  - Do not save the DataType* retrieved from SharedDataPtr if it can be avoided.
 *
 */
template<typename SD>
class SharedDataPtr
{
public:
    using SharedDataType = SD;
    using DataType = typename SharedDataType::DataType;

    /**
     * @brief Constructor
     *
     * @param SharedDataType the instance to wrap.
     * @param stable_read true means DataType* will only be read once, by calling reader_ready().
     *                    false means that reader_ready() is called every time the DataType* is accessed.
     *                    stable_read = false is almost always the correct solution
     */
    SharedDataPtr(SharedDataType* shared_data, bool stable_read = false);
    SharedDataPtr(SharedDataPtr&& rhs) = default;
    SharedDataPtr& operator=(SharedDataPtr&& rhs) = default;

    // Ptr to DataType
    const DataType* operator->();

    // Ptr to DataType
    const DataType* get();

    ~SharedDataPtr();
private:
    SharedDataType* m_shared_data;
    DataType*       m_pCurrentData;
    const bool      m_stable_read;
};

// Convenience maker of SharedDataPtr
template<typename SD>
SharedDataPtr<SD> make_shared_data_ptr(SD* sd, bool stable_read = false)
{
    return SharedDataPtr<SD>(sd, stable_read);
}


/// IMPLEMENTATION
///

extern CachelineAtomic<int64_t> num_collector_updates;
extern CachelineAtomic<int64_t> num_collector_copies;
extern CachelineAtomic<int64_t> num_shareddata_collector_blocks;
extern CachelineAtomic<int64_t> num_shareddata_worker_blocks;   // <-- Rapid growth means something is wrong
extern CachelineAtomic<int64_t> num_collector_cap_waits;        // <-- Rapid growth means something is wrong

/**
 * For tweaking and debug. This just formats the counters above into a string.
 */
std::string get_collector_stats();

template<typename Data, typename Update>
SharedData<Data, Update>::SharedData(DataType* pData,
                                     int max_updates,
                                     std::condition_variable* Collector_wakeup,
                                     bool* pData_rdy)
    : m_queue_max(max_updates)
    , m_pCollector_wakeup(Collector_wakeup)
    , m_pData_rdy(pData_rdy)
{
    m_queue.reserve(m_queue_max);
    m_pCurrent.store(pData, std::memory_order_relaxed);
    m_pNew.store(pData, std::memory_order_relaxed);
}

template<typename Data, typename Update>
void SharedData<Data, Update>::set_new_data(const DataType* pData)
{
    std::unique_lock<std::mutex> guard(m_ptr_exchange_mutex);
    m_pNew.store(pData, std::memory_order_release);
}

template<typename Data, typename Update>
std::pair<const Data*, const Data*> SharedData<Data, Update>::get_ptrs() const
{
    std::unique_lock<std::mutex> guard(m_ptr_exchange_mutex);
    const DataType* ptr1 = m_pCurrent.load(std::memory_order_acquire);
    const DataType* ptr2 = m_pNew.load(std::memory_order_acquire);

    return {ptr1, ptr2};
}

template<typename Data, typename Update>
bool SharedData<Data, Update>::wait_for_updates(maxbase::Duration timeout, std::atomic<bool>* pNo_blocking)
{
    // The Collector can call this on any instance of its SharedDatas
    std::unique_lock<std::mutex> guard(m_update_mutex);

    bool ret_got_data = false;
    auto pred = [this, pNo_blocking]() {
        return *m_pData_rdy || (*pNo_blocking).load(std::memory_order_relaxed);
    };

    if (timeout.count() != 0)
    {
        ret_got_data = m_pCollector_wakeup->wait_for(guard, timeout, pred);
    }
    else
    {
        m_pCollector_wakeup->wait(guard, pred);
        ret_got_data = true;
    }

    mxb_assert(pred() || !ret_got_data);

    // Since this flag is used as the predicate for the condition variable, it must be set to false only after
    // the condition variable has been waited on.
    *m_pData_rdy = false;

    return ret_got_data;
}

template<typename Data, typename Update>
bool SharedData<Data, Update>::get_updates(std::vector<UpdateType>& swap_me)
{
    std::unique_lock<std::mutex> guard(m_update_mutex, std::defer_lock);

    if (!guard.try_lock())
    {
        num_shareddata_collector_blocks.fetch_add(1, std::memory_order_release);
        return false;
    }

    swap_me.swap(m_queue);

    num_collector_updates.fetch_add(swap_me.size(), std::memory_order_release);

    m_data_swapped_out = true;
    m_worker_wakeup.notify_one();

    return true;
}

template<typename Data, typename Update>
void SharedData<Data, Update>::reset_ptrs()
{
    m_pCurrent.store(0, std::memory_order_release);
    m_pNew.store(0, std::memory_order_release);
}

template<typename Data, typename Update>
template<typename ...Params>
void SharedData<Data, Update>::send_update(Params&&... args)
{
    std::unique_lock<std::mutex> guard(m_update_mutex);

    for (bool done = false; !done;)
    {
        if (m_queue.size() < m_queue_max)
        {
            m_queue.emplace_back(std::forward<Params>(args)...);
            *m_pData_rdy = true;
            m_pCollector_wakeup->notify_one();
            done = true;
        }
        else
        {
            num_shareddata_worker_blocks.fetch_add(1, std::memory_order_relaxed);
            m_data_swapped_out = false;
            m_worker_wakeup.wait(guard, [this]() {
                return m_data_swapped_out;
            });
        }
    }
}

template<typename Data, typename Update>
bool SharedData<Data, Update>::has_data() const
{
    return !m_queue.empty();
}

template<typename Data, typename Update>
const Data* SharedData<Data, Update>::reader_ready()
{
    std::unique_lock<std::mutex> guard(m_ptr_exchange_mutex);
    const DataType* new_ptr = m_pNew.load(std::memory_order_acquire);
    m_pCurrent.store(new_ptr, std::memory_order_release);
    return new_ptr;
}

template<typename Data, typename Update>
void SharedData<Data, Update>::shutdown()
{
    // The workers have already stopped and their threads joined. The gcCollector can get stuck
    // because of that, so Collector<SD>::stop() calls this on one (any) SharedData instance.
    std::unique_lock<std::mutex> guard(m_update_mutex);

    *m_pData_rdy = true;
    m_pCollector_wakeup->notify_one();
}

template<typename SD>
SharedDataPtr<SD>::SharedDataPtr(SharedDataType* shared_data, bool stable_read)
    : m_shared_data{shared_data}
    , m_pCurrentData(const_cast<typename SharedDataType::DataType*>(shared_data->reader_ready()))
    , m_stable_read(stable_read)
{
}

template<typename SD>
const typename SD::DataType* SharedDataPtr<SD>::operator->()
{
    return get();
}

template<typename SD>
const typename SD::DataType* SharedDataPtr<SD>::get()
{
    if (!m_stable_read)
    {
        m_pCurrentData = const_cast<typename SharedDataType::DataType*>(m_shared_data->reader_ready());
    }
    return m_pCurrentData;
}

template<typename SD>
SharedDataPtr<SD>::~SharedDataPtr()
{
    m_shared_data->reader_ready();
}
}
