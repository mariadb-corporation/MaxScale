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

#include <maxbase/shareddata.hh>
#include <maxbase/threadpool.hh>
#include <maxbase/string.hh>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iomanip>

namespace maxbase
{

/**
 *  @class Collector
 *
 *  The Collector runs in a single thread processing updates of the DataType
 *  of a SharedData<DataType, UpdateType>.
 *  The update process creates new instances of DataType which are garbage collected once they
 *  are unreachable by all workers (a.k.a clients, a.k.a readers).
 *
 *  Collector has two pure abstract functions: create_new_copy() and make_updates() for handling the
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
 *  /// Complete Collector subclass for a std::unordered_map
 *  class CacheUpdater : public Collector<SharedCache>
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
 *                             std::vector<typename SharedCache::UpdateType>& queue) override
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
 */

template<typename SD>
class Collector
{
public:
    using SharedDataType = SD;
    using DataType = typename SharedDataType::DataType;

    /**
     * @brief Constructor
     *
     * @param pInitialData The initial DataType instance.
     * @param num_clients Number of SharedData.
     *        NOTE: if the client implements dynamic threads, thus calling
     *              increase/decrease_client_count(), it must pass
     *              num_clients==0.
     * @param queue_max The max queue length in a SharedData
     * @param cap_copies Maximum number of simultaneous copies of SD::DataType
     *                   if <= 0, the number of copies is unlimited
     * @param updates_only means that the Collector will only handle updates and not
     *        provide the read-back interface.
     *        This turns off pointer creation and garbage collection.
     *        The clients do not need to call reader_ready() on their SharedData,
     *        but reader_ready() will still be valid returning pInitialData. This
     *        can be used for shared "const" data for the workers, e.g. a Context class.
     *        This mode is for Collector subclasses implementing e.g. a logger or where
     *        the updates are accumulated to be read by some other mechanism (for
     *        example collecting statistics). In the latter case it is up to the
     *        implementation to decide if that structure is accumulated into
     *        pInitialData or something else.
     */
    Collector(std::unique_ptr<DataType> sInitial_copy,
              int num_clients,
              int queue_max,
              int cap_copies,
              bool updates_only = false);

    void start();
    void stop();

    // Add a SharedData to the end of the vector.
    // The index is passed in for asserting that it matches expectations.
    void increase_client_count(size_t index);

    // Drop the last SharedData (highest index).
    // The index is passed in for asserting that it matches expectation.
    void decrease_client_count(size_t index);

    // The SharedDataType instances are owned by Collector, get pointers to all of them...
    std::vector<SharedDataType*> get_shared_data_pointers();

    // ... alternatively, if the threads using SD are ordered [0, num_clients[,
    // this may be more convenient.
    SharedDataType* get_shared_data_by_index(int thread_id);

    // Only for testing. The pointed to data may be collected (deleted) at any time, the caller
    // must know what it is doing.
    DataType* get_pLatest();

private:
    void run();
    int  gc();
    void read_clients(std::vector<int> clients);

    std::vector<const DataType*> get_in_use_ptrs();

    std::atomic<bool> m_running;
    std::thread       m_thread;
    DataType*         m_pLatest_data;

    // Synchronize the updater thread and an increase/decrease_client_count() call
    std::mutex              m_client_count_mutex;
    std::condition_variable m_client_cond;
    std::atomic<bool>       m_pending_client_change{false};
    std::atomic<bool>       m_no_blocking{false};

    size_t           m_queue_max;   // of a SharedData instance
    int              m_cap_copies;
    bool             m_updates_only;
    std::vector<int> m_client_indices;

    std::vector<std::unique_ptr<SharedDataType>>          m_shared_data;
    std::vector<const typename SharedDataType::DataType*> m_all_ptrs;
    std::vector<typename SharedDataType::UpdateType>      m_local_queue;
    std::vector<typename SharedDataType::UpdateType>      m_swap_queue;

    std::condition_variable m_updater_wakeup;
    bool                    m_data_rdy {false};

    void update_client_indices();

    virtual std::unique_ptr<DataType> create_new_copy(const DataType*)
    {
        /// Misconfigured updater. Either turn off the updates_only feature or
        /// implement create_new_copy.
        mxb_assert(!true);
        return nullptr;
    }

    // The queue is never empty
    virtual void make_updates(typename SharedDataType::DataType* pData,
                              std::vector<typename SharedDataType::UpdateType>& queue) = 0;
};

/// IMPLEMENTATION
///
///
template<typename SD>
Collector<SD>::Collector(std::unique_ptr<DataType> sInitial_copy,
                         int num_clients,
                         int queue_max,
                         int cap_copies,
                         bool updates_only)
    : m_running(false)
    , m_pLatest_data(sInitial_copy.release())
    , m_queue_max(queue_max)
    , m_cap_copies(cap_copies)
    , m_updates_only(updates_only)
{
    mxb_assert(cap_copies != 1);

    m_swap_queue.reserve(m_queue_max);
    m_all_ptrs.push_back(m_pLatest_data);

    for (int i = 0; i < num_clients; ++i)
    {
        m_shared_data.push_back(std::make_unique<SharedDataType>(m_pLatest_data, m_queue_max,
                                                                 &m_updater_wakeup, &m_data_rdy));
    }

    update_client_indices();
}

template<typename SD>
void Collector<SD>::read_clients(std::vector<int> clients)
{
    while (!clients.empty())
    {
        int index = clients.back();
        m_swap_queue.clear();

        if (m_shared_data[index]->get_updates(m_swap_queue))
        {
            m_local_queue.insert(end(m_local_queue),
                                 std::make_move_iterator(begin(m_swap_queue)),
                                 std::make_move_iterator(end(m_swap_queue)));
            clients.pop_back();
        }
        else
        {   // the client was busy, check others first
            std::rotate(begin(clients), begin(clients) + 1, end(clients));
        }
    }
}

template<typename SD>
std::vector<const typename SD::DataType*> Collector<SD>::get_in_use_ptrs()
{
    std::vector<const DataType*> in_use_ptrs;
    in_use_ptrs.reserve(2 * m_shared_data.size());
    for (auto& c : m_shared_data)
    {
        auto ptrs = c->get_ptrs();
        in_use_ptrs.push_back(ptrs.first);
        in_use_ptrs.push_back(ptrs.second);
    }

    std::sort(begin(in_use_ptrs), end(in_use_ptrs));
    in_use_ptrs.erase(std::unique(begin(in_use_ptrs), end(in_use_ptrs)), end(in_use_ptrs));

    return in_use_ptrs;
}

template<typename SD>
void Collector<SD>::update_client_indices()
{
    m_client_indices.resize(m_shared_data.size());
    std::iota(begin(m_client_indices), end(m_client_indices), 0);       // 0, 1, 2, ...
}

template<typename SD>
void Collector<SD>::run()
{
    std::unique_lock client_lock(m_client_count_mutex);

    static std::atomic<int> instance_ctr{-1};
    auto name {MAKE_STR("Collector-" << std::setw(2) << std::setfill('0') << ++instance_ctr)};
    maxbase::set_thread_name(m_thread, name);

    const maxbase::Duration garbage_wait_tmo {std::chrono::microseconds(100)};
    int gc_ptr_count = 0;

    // Initially the threads may not yet have been created
    while (m_running.load(std::memory_order_acquire) && m_client_indices.size() == 0)
    {
        client_lock.unlock();
        std::this_thread::sleep_for(garbage_wait_tmo);
        client_lock.lock();
    }

    while (m_running.load(std::memory_order_acquire))
    {
        m_client_cond.wait(client_lock, [this]() {
            return !m_pending_client_change.load(std::memory_order_acquire);
        });

        m_local_queue.clear();
        read_clients(m_client_indices);

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
                int count = 5;
                while (gc_ptr_count
                       && --count
                       && !(have_data = m_shared_data[0]->wait_for_updates(garbage_wait_tmo, &m_no_blocking)))
                {
                    gc_ptr_count = gc();
                }
            }

            if (!have_data && m_running.load(std::memory_order_acquire))
            {
                m_shared_data[0]->wait_for_updates(0s, &m_no_blocking);
            }

            read_clients(m_client_indices);

            if (m_local_queue.empty())
            {
                // The Collector is either shutting down or the non-blocking read_clients() call consumed all
                // the events. The wait_for_updates() call can "spuriously" wake up due to events being
                // consumed before the notifications are read. If wait_for_updates() was called before each
                // read_clients() call (currently it isn't), we could assert at this point that the Collector
                // is shutting down if the local queue is empty.
                continue;
            }
        }

        mxb_assert(!m_shared_data.size() || m_local_queue.size() < m_shared_data.size() * m_queue_max);

        while (m_cap_copies > 0
               && gc_ptr_count >= m_cap_copies
               && m_running.load(std::memory_order_acquire))
        {
            // wait for workers to release more data, it should be over very quickly since there
            // needs to be only one to release with current logic (but that may change in the future).
            num_collector_cap_waits.fetch_add(1, std::memory_order_relaxed);

            auto before = gc_ptr_count;
            gc_ptr_count = gc();
            if (before == gc_ptr_count)
            {
                std::this_thread::sleep_for(garbage_wait_tmo);
            }
        }

        if (!m_updates_only)
        {
            m_pLatest_data = create_new_copy(m_pLatest_data).release();
            num_collector_copies.fetch_add(1, std::memory_order_relaxed);

            m_all_ptrs.push_back(m_pLatest_data);

            ++gc_ptr_count;
        }

        make_updates(m_pLatest_data, m_local_queue);

        if (!m_updates_only)
        {
            for (auto& s : m_shared_data)
            {
                s->set_new_data(m_pLatest_data);
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
        s->reset_ptrs();
    }

    gc();
}

template<typename SD>
void Collector<SD>::start()
{
    std::unique_lock client_lock(m_client_count_mutex);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&Collector<SharedDataType>::run, this);
}

template<typename SD>
void Collector<SD>::stop()
{
    m_running.store(false, std::memory_order_release);

    // The client count may have gone to zero (dynamic threading)
    bool have_clients = false;
    for (auto& s : m_shared_data)
    {
        have_clients = true;
        s->reset_ptrs();    // release data for garbage collection
    }

    if (have_clients)
    {
        // Roundabout way to notify m_thread to wake up to
        // perform shutdown. The thread does not actually
        // block anymore, but should the functionality change,
        // this prevents an unwelcome surprise.
        m_shared_data[0]->shutdown();
    }

    m_thread.join();
}

template<typename SD>
void Collector<SD>::increase_client_count(size_t index)
{
    mxb_assert(index == m_shared_data.size());

    m_pending_client_change.store(true, std::memory_order_release);
    m_no_blocking.store(true, std::memory_order_release);
    m_updater_wakeup.notify_one();
    std::lock_guard client_lock(m_client_count_mutex);

    m_shared_data.push_back(std::make_unique<SharedDataType>(m_pLatest_data,
                                                             m_queue_max,
                                                             &m_updater_wakeup,
                                                             &m_data_rdy));
    update_client_indices();
    m_pending_client_change.store(false, std::memory_order_release);
    m_no_blocking.store(false, std::memory_order_release);
    m_client_cond.notify_one();
}

template<typename SD>
void Collector<SD>::decrease_client_count(size_t index)
{
    mxb_assert(index + 1 == m_shared_data.size());

    m_pending_client_change.store(true, std::memory_order_release);
    m_no_blocking.store(true, std::memory_order_release);
    m_updater_wakeup.notify_one();
    std::unique_lock client_lock(m_client_count_mutex);

    while (m_shared_data.back()->has_data())
    {
        m_pending_client_change.store(false, std::memory_order_release);
        client_lock.unlock();
        m_client_cond.notify_one();

        std::this_thread::sleep_for(1ms);
        m_pending_client_change.store(true, std::memory_order_release);
        client_lock.lock();
    }

    m_pending_client_change.store(false, std::memory_order_release);
    m_no_blocking.store(false, std::memory_order_release);

    m_shared_data.resize(m_shared_data.size() - 1);

    if (index == 0)
    {
        m_running = false;
        m_client_indices.clear();
    }
    else
    {
        update_client_indices();
    }

    m_client_cond.notify_one();
}

template<typename SD>
int Collector<SD>::gc()
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
std::vector<SD*> Collector<SD>::get_shared_data_pointers()
{
    std::vector<SD*> ptrs;
    for (auto& c : m_shared_data)
    {
        ptrs.push_back(c.get());
    }

    return ptrs;
}

template<typename SD>
SD* Collector<SD>::get_shared_data_by_index(int thread_id)
{
    return m_shared_data[thread_id].get();
}

template<typename SD>
typename SD::DataType* Collector<SD>::get_pLatest()
{
    return m_pLatest_data;
}
}
