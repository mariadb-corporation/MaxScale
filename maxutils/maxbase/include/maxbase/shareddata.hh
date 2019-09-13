/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#include <maxbase/assert.h>

#include <functional>
#include <atomic>
#include <thread>
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
 *  This class represents data shared across multiple threads. Together with an updater this is
 *  essentially a multiple reader single writer, where updates to the data by the readars are
 *  handled in a writer/updater thread.
 *
 *  Motivation: Make reading shared, cached data a zero overhead operation
 *
 *  Worker - a thread reading data and sending updates
 *  Updater - a thread that reads and processes the updates.
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
 *  string with "breaking news" from GNN. It is easy to write specialized Updaters that can perform e.g.
 *  cumulative updates to some data, or, keep track of the 10 most used queries during the past 10 minutes.
 *
 *  SharedData contains two pointers to some data type, the current and the new pointer. Each Reader
 *  has its own copy of SharedData and can always read the current data without any locks by following
 *  the simple protocol of calling Data* ptr = reader_ready() at the top of their loop. All that
 *  reader_ready() does is to unconditionally copy the new pointer to the current pointer, and return
 *  the current pointer. Technically reader_ready() can be called at any time, but it is a safer practice
 *  to only call it once at the top of the loop (and store it to a member variable if needed).
 *
 *  Updates to "data" are handled by a specialized GCUpdater. A worker makes an update using the
 *  ShareDataType::send_update() function. The associated GCUpdater will do what a regular a CAS
 *  reader/writer update would do but in a separate thread, with much more control of the overhead.
 *
 *  The ordering and causality of updates can be handled at various levels of strictness by an instance
 *  of a specialized GCUpdater and some co-operation from the Workers, obviously with a cost to system
 *  performance. However, disregarding causality and read-back, GCUpdater can process updates across all
 *  workers in the order the updates were created. Total ordering is on by default, if it is not needed,
 *  it pays to turn it off. For a thread to be able to immediately read back an update, it would
 *  be possible to add an intermediate cache layer (which could cause causality issues).
 *  At the time of writing, read-back and causality are not issues.
 *
 *  The queue of updates is in the SharedData instance, the instance of which is only shared by one
 *  worker and one updater (GCUpdater). There are three reasons for this, first and foremost data
 *  locality (low cache-memory pressure), second, it enables the use of an efficient lock-free
 *  single-producer-single-consumer algorithm, third, it scales very well to a large numbers of cores.
 *  (need a reference here, or something magic in the updater, which WILL become the bottleneck with
 *  lots cores and lots of updates, the latter of which is presumed NOT to happen).
 *
 *  See full example in gcupdater.hh
 *
 *  @tparam Data the type being shared. For example std::map<std::string, std::string>.
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
class alignas (CachelineAlignment)SharedData
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
    explicit SharedData(Data * pData, int max_updates);

    /**
     * @brief A reader/worker thread should call this each loop to get a ptr to a fresh copy of DataType.
     *
     * @return Pointer to the latest copy of DataType.
     */
    const Data* reader_ready();

    /**
     * @brief A reader/worker calls this function to update DataType. The actual update will happen at some
     *        later time.
     *
     * @param update A suitable instance of UpdateType, which becomes InternalUpdate when queued up.
     */
    void send_update(const Update& update);

    // InternalUpdate adds a timestamp (order of creation) to UpdateType, for use by a GCUpdater and
    // it's subclasses.
    struct InternalUpdate
    {
        UpdateType update;
        int64_t    tstamp;
    };

    // For GCUpdater, so it can move SharedData into a vector (in initialization)
    SharedData(SharedData && rhs);
private:
    // GCUpdater is a friend. All private functions are for GCUpdater, and nothing else, to call.
    template<typename Me>
    friend class GCUpdater;

    void set_new_data(const Data* pData);
    bool get_updates(std::vector<InternalUpdate>& swap_me);
    void reset_ptrs();

    std::pair<const Data*, const Data*> get_ptrs() const;

    std::vector<InternalUpdate> m_queue;
    size_t                      m_queue_max;

    std::atomic<const Data*> m_pCurrent;
    std::atomic<const Data*> m_pNew;
    std::atomic_flag         m_atomic_flag {0};     // spinlock for get_updates() and send_update()
};

/// IMPLEMENTATION
///

// TODO there needs to be one timestamp (integer sequence) generator per
//      instantiated GCWorker for update ordering.
extern CachelineAtomic<int64_t> shareddata_timestamp_generator;
extern CachelineAtomic<int64_t> num_shareddata_updater_blocks;
extern CachelineAtomic<int64_t> num_shareddata_worker_blocks;

template<typename Data, typename Update>
SharedData<Data, Update>::SharedData(Data* pData, int max_updates)
    : m_queue_max(max_updates)
{
    m_queue.reserve(m_queue_max);
    m_pCurrent.store(pData, std::memory_order_relaxed);
    m_pNew.store(pData, std::memory_order_relaxed);
}

template<typename Data, typename Update>
SharedData<Data, Update>::SharedData(SharedData&& rhs)
    : m_queue(std::move(rhs.m_queue))
    , m_queue_max(rhs.m_queue_max)
    , m_pCurrent(rhs.m_pCurrent.load())
    , m_pNew(rhs.m_pNew.load())
    , m_atomic_flag {0}
{
}

template<typename Data, typename Update>
void SharedData<Data, Update>::set_new_data(const Data* pData)
{
    m_pNew.store(pData, std::memory_order_release);
}

template<typename Data, typename Update>
std::pair<const Data*, const Data*> SharedData<Data, Update>::get_ptrs() const
{
    const Data* ptr1 = m_pCurrent.load(std::memory_order_acquire);
    const Data* ptr2 = m_pNew.load(std::memory_order_acquire);

    return {ptr1, ptr2};
}

template<typename Data, typename Update>
bool SharedData<Data, Update>::get_updates(std::vector<InternalUpdate>& swap_me)
{
    if (m_atomic_flag.test_and_set(std::memory_order_acq_rel))
    {
        num_shareddata_updater_blocks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    swap_me.swap(m_queue);
    m_atomic_flag.clear(std::memory_order_acq_rel);
    return true;
}

template<typename Data, typename Update>
void SharedData<Data, Update>::reset_ptrs()
{
    m_pCurrent.store(0, std::memory_order_release);
    m_pNew.store(0, std::memory_order_release);
}

template<typename Data, typename Update>
void SharedData<Data, Update>::send_update(const Update& update)
{
    bool done = false;
    InternalUpdate iu {update, shareddata_timestamp_generator.fetch_add(1, std::memory_order_release)};
    while (!done)
    {
        while (m_atomic_flag.test_and_set(std::memory_order_acq_rel))
        {
            num_shareddata_worker_blocks.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }

        if (m_queue.size() < m_queue_max)
        {
            m_queue.push_back(iu);
            done = true;
        }

        m_atomic_flag.clear(std::memory_order_acq_rel);
    }
}

template<typename Data, typename Update>
const Data* SharedData<Data, Update>::reader_ready()
{
    const Data* new_ptr = m_pNew.load(std::memory_order_acquire);
    m_pCurrent.store(new_ptr, std::memory_order_release);
    return new_ptr;
}
}
