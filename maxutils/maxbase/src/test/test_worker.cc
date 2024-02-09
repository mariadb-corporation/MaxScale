/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/assert.hh>
#include <maxbase/maxbase.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/worker.hh>

using namespace maxbase;
using namespace std;

namespace
{

// TODO: Put this in some common place.
int64_t get_monotonic_time_ms()
{
    struct timespec ts;
    MXB_AT_DEBUG(int rv = ) clock_gettime(CLOCK_MONOTONIC, &ts);
    mxb_assert(rv == 0);

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

class TimerTest : public Worker::Callable
{
public:
    static int s_ticks;

    TimerTest(Worker* pWorker,
              int* pRv,
              const std::chrono::milliseconds& delay,
              bool cancel_at_destruct = true)
        : Worker::Callable(pWorker)
        , m_id(s_id++)
        , m_worker(*pWorker)
        , m_delay(delay)
        , m_at(get_monotonic_time_ms() + delay.count())
        , m_rv(*pRv)
        , m_cancel_at_destruct(cancel_at_destruct)
    {
    }

    ~TimerTest()
    {
        if (m_cancel_at_destruct)
        {
            cancel_dcall(m_dcid);
        }
    }

    std::chrono::milliseconds delay() const
    {
        return m_delay;
    }

    void start()
    {
        m_dcid = dcall(delay(), &TimerTest::tick, this);
    }

    bool tick(Callable::Action action)
    {
        bool rv = false;

        if (action == Callable::EXECUTE)
        {
            int64_t now = get_monotonic_time_ms();
            int64_t diff = abs(now - m_at);

            cout << m_id << ": " << diff << endl;

            if (diff > 50)
            {
                cout << "Error: Difference between expected and happened > 50: " << diff << endl;
                m_rv = EXIT_FAILURE;
            }

            m_at += m_delay.count();

            if (--s_ticks < 0)
            {
                m_worker.shutdown();
            }

            rv = true;
        }

        return rv;
    }

private:
    static int s_id;

    int                       m_id;
    Worker&                   m_worker;
    std::chrono::milliseconds m_delay;
    int64_t                   m_at;
    int&                      m_rv;
    Worker::DCId              m_dcid { 0 };
    bool                      m_cancel_at_destruct;
};

int TimerTest::s_id = 1;
int TimerTest::s_ticks;

int run_timer_test()
{
    int rv = EXIT_SUCCESS;

    TimerTest::s_ticks = 100;

    Worker w;

    TimerTest t1(&w, &rv, 200ms);
    TimerTest t2(&w, &rv, 300ms);
    TimerTest t3(&w, &rv, 400ms);
    TimerTest t4(&w, &rv, 500ms);
    TimerTest t5(&w, &rv, 600ms);
    auto cancel_at_destruct = false;
    TimerTest* pT6 = new TimerTest(&w, &rv, 500ms, cancel_at_destruct);

    w.execute([&]() {
                  t1.start();
                  t2.start();
                  t3.start();
                  t4.start();
                  t5.start();
                  pT6->start();

                  delete pT6;
              }, mxb::Worker::EXECUTE_QUEUED);

    w.run();

    return rv;
}

class MoveTest : public Worker::Callable
{
public:
    MoveTest(Worker* pW1, Worker* pW2, Worker* pW3)
        : Worker::Callable(pW1)
        , m_pW(pW1)
        , m_pW1(pW1)
        , m_pW2(pW2)
        , m_pW3(pW3)
    {
    }

    ~MoveTest()
    {
        cancel_dcalls();
    }

    void start()
    {
        cout << "Ping: " << flush;
        dcall(1ms, &MoveTest::ping, this);
    }

    void move()
    {
        m_moving = true;

        auto* pW = worker();
        mxb_assert(pW == m_pW);

        ++m_nMoves;

        cout << "Move(" << m_nMoves << "): " << m_pW << endl;

        m_pW = nullptr;

        if (pW == m_pW1)
        {
            pW = m_pW2;
        }
        else if (pW == m_pW2)
        {
            pW = m_pW3;
        }
        else
        {
            mxb_assert(pW == m_pW3);
            pW = m_pW1;
        }

        set_worker(nullptr);
        pW->execute([this, pW]() {
                set_worker(pW);
                m_pW = pW;

                resume_dcalls();
                m_stopwatch.restart();

                cout << "Ping: " << flush;
                m_moving = false;
            }, mxb::Worker::EXECUTE_QUEUED);
    }

    bool ping(Callable::Action action)
    {
        if (action == Callable::CANCEL)
        {
            return false;
        }

        mxb_assert(!m_moving);

        auto* pW = worker();
        mxb_assert(pW == m_pW);

        cout << "." << flush;

        if (m_stopwatch.split() > std::chrono::milliseconds(10))
        {
            cout << endl;

            if (m_nMoves < 1000)
            {
                suspend_dcalls();

                pW->execute([this](){
                        move();
                    }, mxb::Worker::EXECUTE_QUEUED);
            }
            else
            {
                m_pW3->shutdown();
                m_pW2->shutdown();
                m_pW1->shutdown();
            }
        }

        return true;
    }

private:
    Worker*        m_pW { nullptr };
    Worker*        m_pW1;
    Worker*        m_pW2;
    Worker*        m_pW3;
    int32_t        m_nMoves { 0 };
    mxb::StopWatch m_stopwatch;
    bool           m_moving { false };
};

void run_move_test()
{
    Worker w1;
    Worker w2;
    Worker w3;

    MoveTest m(&w1, &w2, &w3);

    w1.execute([&]() {
            m.start();
        }, mxb::Worker::EXECUTE_QUEUED);

    w3.start("w3");
    w2.start("w2");
    w1.run();

    w3.join();
    w2.join();
}

}

int main()
{
    mxb::MaxBase mxb(MXB_LOG_TARGET_STDOUT);

    int rv = 0;
    rv = run_timer_test();
    run_move_test(); // Expected to crash, if there are issues.

    return rv;
}
