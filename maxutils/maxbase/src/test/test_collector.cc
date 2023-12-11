/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/gcupdater.hh>
#include <iostream>

// This test checks that updates are correctly ordered
// during update processing in a GCUpdater subclass.

// For the specific bug that prompted this test, the queue
// length (queue from a SharedData to GCUpdater) should be
// short, there must be more than 4 "worker" threads and
// they need to be slow compared to the updater but fast
// enough to update in parallel, thus a short sleep in them
// in the code below.

struct TestContext
{
};

struct TestUpdate
{
    std::string str;
};

using SharedTestUpdate = maxbase::SharedData<TestContext, TestUpdate>;

class TestCollector : public maxbase::GCUpdater<SharedTestUpdate>
{
public:
    TestCollector()
        : maxbase::GCUpdater<SharedTestUpdate>(
            new TestContext {},
            6,      // nthreads
            2,      // Queue length.
            0,      // Cap, not used in updates_only mode
            true,   // ordered
            true)   // update only
    {
    }

    bool success()
    {
        return m_success;
    }

private:
    void make_updates(TestContext*,
                      std::vector<SharedTestUpdate::InternalUpdate>& queue) override
    {
        static bool once = true;
        for (const auto& e : queue)
        {
            if (m_seq_no != e.tstamp)
            {
                if (once)
                {
                    std::cerr << "Sequence error: " << m_seq_no << " expected: " << e.tstamp << std::endl;
                    m_seq_no = e.tstamp;
                    once = false;
                    m_success = false;
                }
            }
            ++m_seq_no;
        }
    }

    int64_t m_seq_no{0};
    bool    m_success{true};
};

static std::atomic<bool> running{true};

class Worker
{
public:
    Worker(SharedTestUpdate* pSd)
        : m_pSd(pSd)
    {
    }

    void start()
    {
        m_thread = std::thread(&Worker::run, this);
    }

    Worker(Worker&&) = default;
    Worker& operator=(Worker&&) = default;

    void join()
    {
        m_thread.join();
    }

private:
    SharedTestUpdate* m_pSd;
    std::thread       m_thread;
    void run()
    {
        while (running.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(10us);
            TestUpdate str{"Hello World!"s};
            m_pSd->send_update(str);
        }
    }
};

int main()
{
    TestCollector collector;
    collector.start();
    std::vector<SharedTestUpdate*> shared_datas = collector.get_shared_data_pointers();
    std::vector<Worker> workers;

    std::copy(begin(shared_datas), end(shared_datas), std::back_inserter(workers));
    std::for_each(begin(workers), end(workers), [](auto& worker) {
        worker.start();
    });

    std::this_thread::sleep_for(2s);

    running = false;
    std::for_each(begin(workers), end(workers), [](auto& worker) {
        worker.join();
    });

    collector.stop();

    return collector.success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
