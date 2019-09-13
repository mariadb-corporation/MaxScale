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

#include <maxbase/shareddata.hh>
#include <vector>
#include <numeric>
#include <algorithm>

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
 *      SharedCache* pShared m_pCache;
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

    void run();
    void stop();

    // The SharedDataType instances are owned by GCUpdater, get pointers to all of them...
    std::vector<SharedDataType*> get_shared_data_pointers();

    // ... alternatively, if the threads using SharedDataType are ordered [0, num_clients[,
    // this may be more convenient.
    SharedDataType* get_shared_data_by_order(int thread_id);

    // Only for testing. The pointed to data may be collected (deleted) at any time, the caller
    // must know what it is doing.
    typename SharedDataType::DataType* get_pLatest();

protected:
    /**
     * @brief This function is part of a GCUpdater initialization and should be called by the
     *        subclass constructor.
     *
     * @param pInitialData the initial DataType instance.
     * @param num_clients number of SharedData
     * @param queue_max the max queue length in a SharedData
     * @param cap_copies Maximum number of simultaneous copies of SharedDataType::DataType
     *                   if <= 0, the number of copies is unlimited
     * @param order_updates when true, process updates in order of creation
     */
    void initialize_shared_data(typename SharedDataType::DataType* pInitialData,
                                int num_clients,
                                int queue_max,
                                int cap_copies = 0,
                                bool order_updates = true);

private:
    int  gc();
    void read_clients(std::vector<int> clients);

    std::vector<const typename SD::DataType*> get_in_use_ptrs();

    bool                               m_initialed = false;
    std::atomic<bool>                  m_running {false};
    typename SharedDataType::DataType* m_pLatest_data = nullptr;

    bool   m_order_updates;
    size_t m_queue_max;     // of a SharedData instance
    int    m_cap_copies;

    std::vector<SharedDataType>                           m_shared_data;
    std::vector<const typename SharedDataType::DataType*> m_all_ptrs;
    std::vector<typename SharedDataType::InternalUpdate>  m_local_queue;
    std::vector<typename SharedDataType::InternalUpdate>  m_leftover_queue;

    virtual typename SharedDataType::DataType* create_new_copy(
        const typename SharedDataType::DataType* pCurrent) = 0;

    virtual void make_updates(typename SharedDataType::DataType* pData,
                              std::vector<typename SharedDataType::InternalUpdate>& queue) = 0;
};

/// IMPLEMENTATION
///

template<typename SD>
void GCUpdater<SD>::initialize_shared_data(typename SD::DataType* initial_copy,
                                           int num_clients,
                                           int queue_max,
                                           int cap_copies,
                                           bool order_updates)
{
    mxb_assert(cap_copies != 1);

    m_pLatest_data = initial_copy;
    m_all_ptrs.push_back(m_pLatest_data);
    m_queue_max = queue_max;
    m_cap_copies = cap_copies;
    m_order_updates = order_updates;

    for (int i = 0; i < num_clients; ++i)
    {
        m_shared_data.emplace_back(m_pLatest_data, m_queue_max);
    }

    m_initialed = true;
}

template<typename SD>
void GCUpdater<SD>::read_clients(std::vector<int> clients)
{
    while (!clients.empty())
    {
        int index = clients.back();
        std::vector<typename SharedDataType::InternalUpdate> swap_queue;
        swap_queue.reserve(m_queue_max);

        if (m_shared_data[index].get_updates(swap_queue))
        {
            m_local_queue.insert(end(m_local_queue), begin(swap_queue), end(swap_queue));
            clients.pop_back();
        }
        else
        {   // the client was busy, check others first
            std::rotate(begin(clients), begin(clients), end(clients));
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
    mxb_assert(m_initialed);

    m_running = true;
    int gc_ptr_count = 0;

    size_t num_clients = m_shared_data.size();
    std::vector<int> client_indices(num_clients);
    std::iota(begin(client_indices), end(client_indices), 0);       // 0, 1, 2, ...

    while (m_running.load(std::memory_order_relaxed))
    {
        m_local_queue.clear();
        if (m_order_updates)
        {
            m_local_queue.swap(m_leftover_queue);
        }

        read_clients(client_indices);

        mxb_assert(m_local_queue.size() < 2 * num_clients * m_queue_max);

        if (m_local_queue.empty())
        {
            if (gc_ptr_count)
            {
                gc_ptr_count = gc();
            }

            std::this_thread::yield();
            continue;
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
            for (int64_t prev_tstamp = m_local_queue[0].tstamp;
                 ind != sz && prev_tstamp == m_local_queue[ind].tstamp - 1;
                 prev_tstamp = m_local_queue[ind].tstamp, ++ind)
            {
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
               && int(get_in_use_ptrs().size()) >= m_cap_copies
               && m_running.load(std::memory_order_relaxed))
        {
            num_gcupdater_cap_waits.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        m_pLatest_data = create_new_copy(m_pLatest_data);

        m_all_ptrs.push_back(m_pLatest_data);

        ++gc_ptr_count;

        make_updates(m_pLatest_data, m_local_queue);

        for (auto& s : m_shared_data)
        {
            s.set_new_data(m_pLatest_data);
        }

        // TODO, how many? Maybe just defer to the subclass, m_cap_copies also affects this.
        if (gc_ptr_count > 1)
        {
            gc();
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
void GCUpdater<SD>::stop()
{
    m_running = false;
}

template<typename SD>
int GCUpdater<SD>::gc()
{
    // Get the ptrs that are in use right now
    auto in_use_ptrs {get_in_use_ptrs()};

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

    return m_all_ptrs.size() - 1;
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
SD* GCUpdater<SD>::get_shared_data_by_order(int thread_id)
{
    return &m_shared_data[thread_id];
}

template<typename SD>
typename SD::DataType* GCUpdater<SD>::get_pLatest()
{
    return m_pLatest_data;
}
}
