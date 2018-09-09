/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "tester.hh"
#include <algorithm>
#include <iostream>
#include <set>
#include "cache.hh"
#include "storagefactory.hh"
// TODO: Move this to a common place.
#include "../../../../../query_classifier/test/testreader.hh"

using maxscale::TestReader;
using namespace std;

//
// class Tester::Thread
//

class Tester::Thread
{
public:
    Thread(Tester::Task* pTask)
        : m_pTask(pTask)
        , m_thread(0)
    {
        mxb_assert(pTask);
    }

    ~Thread()
    {
        mxb_assert(m_thread == 0);
    }

    static Thread from_task(Tester::Task* pTask)
    {
        return Thread(pTask);
    }

    Tester::Task* task()
    {
        return m_pTask;
    }

    void start()
    {
        mxb_assert(m_thread == 0);

        if (pthread_create(&m_thread, NULL, &Thread::thread_main, this) != 0)
        {
            cerr << "FATAL: Could not launch thread." << endl;
            exit(EXIT_FAILURE);
        }
    }

    void wait()
    {
        mxb_assert(m_thread != 0);

        pthread_join(m_thread, NULL);
        m_thread = 0;
    }

    static void start_thread(Thread& thread)
    {
        thread.start();
    }

    static void wait_for_thread(Thread& thread)
    {
        thread.wait();
    }

    void run()
    {
        m_pTask->out() << "Thread started.\n" << flush;
        m_pTask->set_rv(m_pTask->run());
    }

    static void run(Thread* pThread)
    {
        pThread->run();
    }

    static void* thread_main(void* pData)
    {
        run(static_cast<Thread*>(pData));
        return 0;
    }

private:
    Tester::Task* m_pTask;
    pthread_t     m_thread;
};

//
// Tester::Task
//

Tester::Task::Task(std::ostream* pOut)
    : m_out(*pOut)
    , m_terminate(false)
    , m_rv(0)
{
}

Tester::Task::~Task()
{
}

//
// Tester
//

Tester::Tester(ostream* pOut)
    : m_out(*pOut)
{
}

Tester::~Tester()
{
}

// static
GWBUF* Tester::gwbuf_from_string(const std::string& s)
{
    size_t len = s.length();
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* pBuf = gwbuf_alloc(gwbuf_len);

    if (pBuf)
    {
        *((unsigned char*)((char*)GWBUF_DATA(pBuf))) = payload_len;
        *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 1)) = (payload_len >> 8);
        *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 2)) = (payload_len >> 16);
        *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 3)) = 0x00;
        *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 4)) = 0x03;    // COM_QUERY
        memcpy((char*)GWBUF_DATA(pBuf) + 5, s.c_str(), len);
    }

    return pBuf;
}

// static
GWBUF* Tester::gwbuf_from_vector(const std::vector<uint8_t>& v)
{
    size_t len = v.size();
    size_t payload_len = len;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* pBuf = gwbuf_alloc(gwbuf_len);

    if (pBuf)
    {
        *((unsigned char*)((char*)GWBUF_DATA(pBuf))) = payload_len;
        *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 1)) = (payload_len >> 8);
        *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 2)) = (payload_len >> 16);
        *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 3)) = 0x00;
        memcpy((char*)GWBUF_DATA(pBuf) + 4, v.data(), len);
    }

    return pBuf;
}

// static
bool Tester::get_unique_statements(std::istream& in, size_t n_statements, Statements* pStatements)
{
    if (n_statements == 0)
    {
        n_statements = UINT_MAX;
    }

    TestReader::result_t result = TestReader::RESULT_ERROR;

    typedef set<string> StatementsSet;
    StatementsSet statements;

    TestReader reader(in);

    size_t n = 0;
    string statement;
    while ((n < n_statements)
           && ((result = reader.get_statement(statement)) == TestReader::RESULT_STMT))
    {
        if (statements.find(statement) == statements.end())
        {
            // Not seen before
            statements.insert(statement);

            pStatements->push_back(statement);
            ++n;
        }
    }

    return result != TestReader::RESULT_ERROR;
}

// static
bool Tester::get_statements(std::istream& in, size_t n_statements, Statements* pStatements)
{
    if (n_statements == 0)
    {
        n_statements = UINT_MAX;
    }

    TestReader::result_t result = TestReader::RESULT_ERROR;

    TestReader reader(in);

    size_t n = 0;
    string statement;
    while ((n < n_statements)
           && ((result = reader.get_statement(statement)) == TestReader::RESULT_STMT))
    {
        pStatements->push_back(statement);
        ++n;
    }

    return result != TestReader::RESULT_ERROR;
}

// static
bool Tester::get_cache_items(const Statements& statements,
                             const StorageFactory& factory,
                             CacheItems* pItems)
{
    bool success = true;

    Statements::const_iterator i = statements.begin();

    while (success && (i != statements.end()))
    {
        GWBUF* pQuery = gwbuf_from_string(*i);
        if (pQuery)
        {
            CACHE_KEY key;
            cache_result_t result = Cache::get_default_key(NULL, pQuery, &key);

            if (result == CACHE_RESULT_OK)
            {
                pItems->push_back(std::make_pair(key, pQuery));
            }
            else
            {
                mxb_assert(!true);
                success = false;
            }
        }
        else
        {
            mxb_assert(!true);
            success = false;
        }

        ++i;
    }

    return success;
}

// static
bool Tester::get_cache_items(std::istream& in,
                             size_t n_items,
                             const StorageFactory& factory,
                             CacheItems* pItems)
{
    Statements statements;

    bool rv = get_unique_statements(in, n_items, &statements);

    if (rv)
    {
        rv = get_cache_items(statements, factory, pItems);
    }

    return rv;
}

// static
void Tester::clear_cache_items(CacheItems& cache_items)
{
    for (CacheItems::iterator i = cache_items.begin(); i != cache_items.end(); ++i)
    {
        gwbuf_free(i->second);
    }

    cache_items.clear();
}

// static
int Tester::execute(ostream& out, size_t n_seconds, const vector<Task*>& tasks)
{
    vector<Thread> threads;

    transform(tasks.begin(), tasks.end(), back_inserter(threads), &Thread::from_task);

    out << "Starting " << tasks.size() << " threads, running for " << n_seconds << " seconds." << endl;

    for_each(threads.begin(), threads.end(), &Thread::start_thread);

    sleep(n_seconds);

    for_each(tasks.begin(), tasks.end(), &Task::terminate_task);

    for_each(threads.begin(), threads.end(), &Thread::wait_for_thread);

    out << "Threads terminated." << endl;

    int rv;

    if (find_if(tasks.begin(), tasks.end(), &Task::failed) == tasks.end())
    {
        rv = EXIT_SUCCESS;
    }
    else
    {
        rv = EXIT_FAILURE;
    }

    return rv;
}
