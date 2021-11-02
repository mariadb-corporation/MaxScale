/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/shareddata.hh>
#include <vector>
#include <numeric>
#include <algorithm>
#include <future>

namespace maxbase
{

/**
 *  @class GCUpdater
 *
 *  A GCUpdater (Garbage Collecting Updater) is the thread handling updates to the DataType of a
 *  a SharedData<DataType, UpdateType>. The update process creates new instances of the DataType which are
 *  garbage collected once they are unreachable by all workers (a.k.a clients, a.k.a readers).
 *
 *  GCUpdater has two pure abstract functions: create_new_copy() and make_updates() for handling the
 *  copying and updating of DataType.
 *
 *  Example: A plain shared std::unordered_map. There could be a lot more logic going into
 *           the update sent from the workers, and especially the function make_updates().
 *
 *  /// Types
 *  enum class CacheAction {InsertUpdate, Delete};
 *  using CacheContainer = std::unordered_map<std::string, std::string>;
 *  struct CacheUpdate
 *  {
 *      CacheAction action;
 *      std::string key;
 *      std::string value;
 *  };
 *  using SharedCache = SharedData<CacheContainer, CacheUpdate>;
 *
 *
 *  /// Part of a worker
 *  class Worker
 *  {
 *  public:
 *      void Worker::run()
 *      {
 *          while (m_pRunning)
 *          {
 *              m_pContainer = m_pCache->reader_ready();
 *
 *              std::string key = ...;
 *              auto item = m_pContainer->find(key);
 *
 *              ...
 *              m_pCache->send_update(SharedCache::UpdateType {key, value, CacheAction::InsertUpdate});
 *          }
 *      }
 *
 *      SharedCache* m_pCache;
 *      const CacheContainer* m_pContainer;
 *  };
 *
 *
 *  /// Complete GCUpdater subclass for a std::unordered_map
 *  class CacheUpdater : public GCUpdater<SharedCache>
 *  {
 *  public:
 *   CacheUpdater(int num_workers)
 *   {
 *       initialize_shared_data(new CacheContainer(), num_workers, 10000, 2);
 *   }
 *  private:
 *
 *   CacheContainer* create_new_copy(const CacheContainer* pCurrent) override
 *   {
 *       return new CacheContainer {*pCurrent};
 *   }
 *
 *   virtual void make_updates(typename SharedCache::DataType* pData,
 *                             std::vector<typename SharedCache::InternalUpdate>& queue) override
 *   {
 *       for (auto& e : queue)
 *       {
 *           switch (e.update.action)
 *           {
 *           case CacheAction::Delete:
 *               pData->erase(e.update.key);
 *               break;
 *
 *           case CacheAction::InsertUpdate:
 *               {
 *                   auto res = pData->insert({e.update.key, e.update.value});
 *                   if (!res.second)
 *                   {
 *                       res.first->second = e.update.value;
 *                   }
 *               }
 *               break;
 *           }
 *       }
 *   }
 *  };
 *
 *
 *  /// the glue
 *  int main()
 *  {
 *        CacheUpdater updater(num_workers);
 *        auto updater_future = std::async(std::launch::async, &CacheUpdater::run, &updater);
 *
 *        std::vector<std::future<void>> futures;
 *        std::vector<std::unique_ptr<Worker>> m_workers;
 *
 *        for (auto ptr : updater.get_shared_data_pointers())
 *        {
 *            m_workers.emplace_back(new Worker(ptr));
 *        }
 *
 *        for (auto& sWorker : m_workers)
 *        {
 *            futures.push_back(std::async(std::launch::async, &Worker::run, sWorker.get()));
 *        }
 *
 *        ...
 *  }
 *
 *  ***********************************************
 *  About the order in which updates are processed.
 *
 *  GCUpdater assumes that the ShareData instances it owns, and only those instances, use the same integer
 *  sequence generator (timestamp generator) when posting updates. This means that there is an unbroken
 *  (integer) sequence of updates ordered by the time they were created.
 *
 *  Each loop, GCUpdater reads updates from all SharedData instances. While it is reading, the workers are
 *  free to post more updates. This can lead to GCUpdater getting an incomplete sequence, where some updates
 *  are missing. But, and this is what makes total order possible and easy, when something is missing
 *  GCUpdater knows that the missing updates are already posted and will complete the sequence in the next
 *  loop. So, GCUpdater sorts the updates it has read and looks for a missing update. If there is one, it
 *  will only process updates up to the missing one, and leave the rest to be processed the next loop.
 *
 *  A followup of this is the fact that the maximum number of updates GCUpdater can ever have after
 *  reading updates and adding the unprocessed ones, is less than twice the total capacity of the
 *  SharedData instances (2 * num_instances * max_queue_length).
 *
 */
template<typename SD>
class GCUpdater
{
public:
    using SharedDataType = SD;

    /**
     * @brief Constructor
     *
     * @param pInitialData The initial DataType instance.
     * @param num_clients Number of SharedData
     * @param queue_max The max queue length in a SharedData
     * @param cap_copies Maximum number of simultaneous copies of SharedDataType::DataType
     *                   if <= 0, the number of copies is unlimited
     * @param order_updates when true, process updates in order of creation
     * @param updates_only means that the GCUpdater will only handle updates and not
     *        provide the read-back interface.
     *        This turns off pointer creation and garbage collection.
     *        The clients do not need to call reader_ready() on their SharedData,
     *        but reader_ready() will still be valid returning pInitialData, which
     *        could be used for shared "const" data for the workers.
     *        This mode is for GCUpdater subclasses implementing e.g. a logger or where
     *        the updates are accumulated to be read by some other mechanism (for
     *        example collecting statistics). In the latter case it is up to the
     *        implementation to decide if that structure is accumulated into
     *        pInitialData or something else.
     */
    GCUpdater(typename SD::DataType* initial_copy,
              int num_clients,
              int queue_max,
              int cap_copies,
              bool order_updates,
              bool updates_only = false);


    void start();
    void stop();

    // The SharedDataType instances are owned by GCUpdater, get pointers to all of them...
    std::vector<SharedDataType*> get_shared_data_pointers();

    // ... alternatively, if the threads using SharedDataType are ordered [0, num_clients[,
    // this may be more convenient.
    SharedDataType* get_shared_data_by_index(int thread_id);

    // Only for testing. The pointed to data may be collected (deleted) at any time, the caller
    // must know what it is doing.
    typename SharedDataType::DataType* get_pLatest();

private:
    void run();
    int  gc();
    void read_clients(std::vector<int> clients);

    std::vector<const typename SD::DataType*> get_in_use_ptrs();

    std::atomic<bool>                  m_running;
    std::future<void>                  m_future;
    typename SharedDataType::DataType* m_pLatest_data;

    int    m_num_clients;
    size_t m_queue_max;     // of a SharedData instance
    int    m_cap_copies;
    bool   m_order_updates;
    bool   m_updates_only;

    std::vector<SharedDataType>                           m_shared_data;
    std::vector<const typename SharedDataType::DataType*> m_all_ptrs;
    std::vector<typename SharedDataType::InternalUpdate>  m_local_queue;
    std::vector<typename SharedDataType::InternalUpdate>  m_leftover_queue;

    std::condition_variable m_updater_wakeup;
    bool                    m_data_rdy;
    std::atomic<int64_t>    m_timestamp_generator;

    virtual typename SharedDataType::DataType* create_new_copy(
        const typename SharedDataType::DataType* pCurrent)
    {
        /// Misconfigured updater. Either turn off the updates_only feature or
        /// implement create_new_copy.
        mxb_assert(!true);
        return nullptr;
    }

    // The queue is never empty
    virtual void make_updates(typename SharedDataType::DataType* pData,
                              std::vector<typename SharedDataType::InternalUpdate>& queue) = 0;
};

/// IMPLEMENTATION
///
///
template<typename SD>
GCUpdater<SD>::GCUpdater(typename SD::DataType* initial_copy,
                         int num_clients,
                         int queue_max,
                         int cap_copies,
                         bool order_updates,
                         bool updates_only)
    : m_running(false)
    , m_pLatest_data(initial_copy)
    , m_num_clients(num_clients)
    , m_queue_max(queue_max)
    , m_cap_copies(cap_copies)
    , m_order_updates(order_updates)
    , m_updates_only(updates_only)
{
    mxb_assert(cap_copies != 1);
    m_all_ptrs.push_back(m_pLatest_data);

    for (int i = 0; i < m_num_clients; ++i)
    {
        m_shared_data.emplace_back(m_pLatest_data, m_queue_max,
                                   &m_updater_wakeup, &m_data_rdy, &m_timestamp_generator);
    }
}

template<typename SD>
void GCUpdater<SD>::read_clients(std::vector<int> clients)
{
    while (!clients.empty())
    {
        int index = clients.back();
        std::vector<typename SharedDataType::InternalUpdate> swap_queue;
        swap_queue.reserve(m_queue_max);

        // magic value clients.size() <= N: when true, get_updates() blocks if a worker has the mutex.
        if (m_shared_data[index].get_updates(swap_queue, clients.size() <= 4))
        {
            m_local_queue.insert(end(m_local_queue), begin(swap_queue), end(swap_queue));
            clients.pop_back();
        }
        else
        {   // the client was busy, check others first
            std::rotate(begin(clients), begin(clients) + 1, end(clients));
        }
    }
}

template<typename SD>
std::vector<const typename SD::DataType*> GCUpdater<SD>::get_in_use_ptrs()
{
    std::vector<const typename SharedDataType::DataType*> in_use_ptrs;
    in_use_ptrs.reserve(2 * m_shared_data.size());
    for (auto& c : m_shared_data)
    {
        auto ptrs = c.get_ptrs();
        in_use_ptrs.push_back(ptrs.first);
        in_use_ptrs.push_back(ptrs.second);
    }

    std::sort(begin(in_use_ptrs), end(in_use_ptrs));
    in_use_ptrs.erase(std::unique(begin(in_use_ptrs), end(in_use_ptrs)), end(in_use_ptrs));

    return in_use_ptrs;
}


template<typename SD>
void GCUpdater<SD>::run()
{
    const maxbase::Duration garbage_wait_tmo {std::chrono::microseconds(100)};
    int gc_ptr_count = 0;

    std::vector<int> client_indices(m_num_clients);
    std::iota(begin(client_indices), end(client_indices), 0);       // 0, 1, 2, ...

    while (m_running.load(std::memory_order_acquire))
    {
        m_local_queue.clear();
        if (m_order_updates)
        {
            m_local_queue.swap(m_leftover_queue);
        }

        read_clients(client_indices);

        mxb_assert(m_local_queue.size() < 2 * m_num_clients * m_queue_max);

        if (m_local_queue.empty())
        {
            if (gc_ptr_count)
            {
                gc_ptr_count = gc();
            }

            bool have_data = false;

            if (gc_ptr_count)
            {
                // wait for updates, or a timeout to check for new garbage (opportunistic gc)
                while (gc_ptr_count
                       && !(have_data = m_shared_data[0].wait_for_updates(garbage_wait_tmo)))
                {
                    gc_ptr_count = gc();
                }
            }

            if (!have_data && m_running.load(std::memory_order_acquire))
            {
                m_shared_data[0].wait_for_updates(maxbase::Duration {0});
            }

            read_clients(client_indices);

            if (m_local_queue.empty())
            {
                mxb_assert(m_running.load(std::memory_order_acquire) == false);
                continue;
            }
        }

        if (m_order_updates && m_local_queue.size() > 1)
        {
            std::sort(begin(m_local_queue), end(m_local_queue),
                      [](const typename SharedDataType::InternalUpdate& lhs,
                         const typename SharedDataType::InternalUpdate& rhs) {
                          return lhs.tstamp < rhs.tstamp;
                      });

            // Find a discontinuity point in input (missing timestamp)
            size_t ind = 1;
            size_t sz = m_local_queue.size();

            int64_t prev_tstamp = m_local_queue[0].tstamp;
            while (ind != sz && prev_tstamp == m_local_queue[ind].tstamp - 1)
            {
                prev_tstamp = m_local_queue[ind].tstamp;
                ++ind;
            }

            if (ind != sz)
            {
                // move the elements from input[ind, end[, to leftover
                for (size_t ind2 = ind; ind2 != sz; ++ind2)
                {
                    m_leftover_queue.push_back(m_local_queue[ind2]);
                }

                // remove those elements from input
                m_local_queue.resize(ind);
            }
        }

        while (m_cap_copies > 0
               && gc_ptr_count >= m_cap_copies
               && m_running.load(std::memory_order_acquire))
        {
            // wait for workers to release more data, it should be over very quickly since there
            // can be only one to release with current logic (but that may change in the future).
            num_gcupdater_cap_waits.fetch_add(1, std::memory_order_relaxed);

            auto before = gc_ptr_count;
            gc_ptr_count = gc();
            if (before == gc_ptr_count)
            {
                std::this_thread::sleep_for(garbage_wait_tmo);
            }
        }

        if (!m_updates_only)
        {
            m_pLatest_data = create_new_copy(m_pLatest_data);
            num_updater_copies.fetch_add(1, std::memory_order_relaxed);

            m_all_ptrs.push_back(m_pLatest_data);

            ++gc_ptr_count;
        }

        make_updates(m_pLatest_data, m_local_queue);

        if (!m_updates_only)
        {
            for (auto& s : m_shared_data)
            {
                s.set_new_data(m_pLatest_data);
            }
        }

        // TODO, how many? Maybe just defer to the subclass, m_cap_copies also affects this.
        if (gc_ptr_count > 1)
        {
            gc_ptr_count = gc();
        }
    }

    // Workers should not be touching shared data any more,
    // they should all have been stopped and joined by now.
    for (auto& s : m_shared_data)
    {
        s.reset_ptrs();
    }

    gc();
}

template<typename SD>
void GCUpdater<SD>::start()
{
    m_running.store(true, std::memory_order_release);
    m_future = std::async(std::launch::async, &GCUpdater<SD>::run, this);
}

template<typename SD>
void GCUpdater<SD>::stop()
{
    m_running.store(false, std::memory_order_release);
    for (auto& s : m_shared_data)
    {
        s.reset_ptrs();
    }
    m_shared_data[0].shutdown();
    m_future.get();
}

template<typename SD>
int GCUpdater<SD>::gc()
{
    // Get the ptrs that are in use right now
    auto in_use_ptrs = get_in_use_ptrs();

    std::sort(begin(m_all_ptrs), end(m_all_ptrs));
    m_all_ptrs.erase(std::unique(begin(m_all_ptrs), end(m_all_ptrs)), end(m_all_ptrs));

    decltype(in_use_ptrs) garbage;

    garbage.reserve(m_all_ptrs.size());

    std::set_difference(begin(m_all_ptrs), end(m_all_ptrs),
                        begin(in_use_ptrs), end(in_use_ptrs),
                        std::back_inserter(garbage));

    m_all_ptrs.swap(in_use_ptrs);

    for (auto trash : garbage)
    {
        delete trash;
    }

    auto sz = m_all_ptrs.size();    // one pointer is the latest, everything else may be gc:ed anytime
    return sz ? sz - 1 : 0;
}

template<typename SD>
std::vector<SD*> GCUpdater<SD>::get_shared_data_pointers()
{
    std::vector<SharedDataType*> ptrs;
    for (auto& c : m_shared_data)
    {
        ptrs.push_back(&c);
    }

    return ptrs;
}

template<typename SD>
SD* GCUpdater<SD>::get_shared_data_by_index(int thread_id)
{
    return &m_shared_data[thread_id];
}

template<typename SD>
typename SD::DataType* GCUpdater<SD>::get_pLatest()
{
    return m_pLatest_data;
}
}
