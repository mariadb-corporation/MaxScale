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

#include <maxbase/collector.hh>
#include <iostream>
#include <maxbase/maxbase.hh>

// This test checks that updates are correctly ordered
// during update processing in a Collector subclass.

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

const int NTHREADS = 6;
const int QUEUE_LEN = 2;
const bool UPDATES_ONLY = true;
// This is what the GCUpdater guarantees with the settings above
const int MAX_EVENTS = 2 * NTHREADS * QUEUE_LEN;

class TestCollector : public mxb::Collector<SharedTestUpdate, mxb::CollectorMode::UPDATES_ONLY>
{
public:
    TestCollector()
        : maxbase::Collector<SharedTestUpdate, mxb::CollectorMode::UPDATES_ONLY>(
            std::make_unique<TestContext>(),
            NTHREADS,   // nthreads
            QUEUE_LEN,  // Queue length.
            0)          // update only
    {
    }

    bool success()
    {
        return m_success;
    }

private:
    void make_updates(TestContext*,
                      std::vector<SharedTestUpdate::UpdateType>& queue) override
    {
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
    mxb::MaxBase mxb(MXB_LOG_TARGET_STDOUT);

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
