#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/buffer.h>
#include <ostream>
#include <string>
#include <vector>
#include <pthread.h>
#include "cache_storage_api.hh"

class StorageFactory;

class Tester
{
public:
    class Task;

    typedef std::vector<std::string> Statements;
    typedef std::vector<std::pair<CACHE_KEY, GWBUF*> > CacheItems;
    typedef std::vector<Task*> Tasks;

    class Thread;
    class Task
    {
    public:
        virtual ~Task();

        /**
         * Called from a thread. Concrete implementation in a derived class
         * is expected to run continuously until @should_terminate returns
         * true. The return value will be stored in the @m_rv member variable.
         *
         * @return EXIT_SUCCESS or EXIT_FAILURE.
         */
        virtual int run() = 0;

        /**
         * Whether the task should terminate. To be called by @run in derived
         * concrete Task class.
         *
         * @return True, if the task should terminate, i.e., return from @c run.
         */
        bool should_terminate() const
        {
            return m_terminate;
        }

        /**
         * Tell the task to terminate.
         */
        void terminate()
        {
            m_terminate = true;
        }

        /**
         * Calls terminate on the provided task. For use in algorithms.
         *
         * @param pTask  The task to terminate.
         */
        static void terminate_task(Task* pTask)
        {
            pTask->terminate();
        }

        /**
         * Deletes the provided task. For use in algorithms.
         *
         * @param pTask  The task to delete.
         */
        static void free(Task* pTask)
        {
            delete pTask;
        }

        /**
         * Predicate for finding out whether a task failed. To be called only
         * after the task has terminated. For use in algorithms.
         *
         * @param pTask  The task to query.
         *
         * @return True, if the task failed.
         */
        static bool failed(const Task* pTask)
        {
            return pTask->rv() == EXIT_FAILURE;
        }

        /**
         * What the @run function returned. Meaningful only after the task
         * has terminated.
         *
         * @return  The value returned by @run.
         */
        int rv() const
        {
            return m_rv;
        }

        /**
         * The stream to be used for user output.
         *
         * @return  The output stream to be used.
         */
        std::ostream& out() const
        {
            return m_out;
        }

    protected:
        /**
         * Constructor
         *
         * @param pOut  Pointer to the stream to use for user output. Note that
         *              the pointer must remain valid for the lifetime of the Task.
         */
        Task(std::ostream* pOut);

    private:
        friend class Thread;
        void set_rv(int rv)
        {
            m_rv = rv;
        }

    private:
        Task(const Task&);
        Task& operator = (const Task&);

    private:
        std::ostream& m_out;
        bool          m_terminate;
        int           m_rv;
    };

    virtual ~Tester();

    /**
     * Converts a string to a COM_QUERY GWBUF.
     *
     * @param s  The string to be converted.
     *
     * @return  A GWBUF or NULL if memory allocation failed.
     */
    static GWBUF* gwbuf_from_string(const std::string& s);

    /**
     * Converts a vector to a GWBUF.
     *
     * NOTE: The data is used verbatim and placed directly after the header; no
     *       interpretation whatsoever.
     *
     * @param v  The vector to be converted.
     *
     * @return  A GWBUF or NULL if memory allocation failed.
     */
    static GWBUF* gwbuf_from_vector(const std::vector<uint8_t>& v);

    /**
     * Returns unique statements from a MySQL/MariaDB server test file.
     *
     * @param in            The stream from which input should be read. Assumed to refer to a
     *                      MySQL/MariaDB test file.
     * @param n_statements  How many statements to return; a value of 0 means no limit.
     * @param pStatements   Pointer to vector where statements will be back inserted.
     *                      May contain less statements that specified in @n_statements.
     *
     * @return  Whether reading was successful, not whether @n_statements statements were returned.
     */
    static bool get_unique_statements(std::istream& in, size_t n_statements, Statements* pStatements);

    /**
     * Returns all statements from a MySQL/MariaDB server test file.
     *
     * @param in            The stream from which input should be read. Assumed to refer to a
     *                      MySQL/MariaDB test file.
     * @param n_statements  How many statements to return; a value of 0 means no limit.
     * @param pStatements   Pointer to vector where statements will be back inserted.
     *                      May contain less statements that specified in @n_statements.
     *
     * @return  Whether reading was successful, not whether @n_statements statements were returned.
     */
    static bool get_statements(std::istream& in, size_t n_statements, Statements* pStatements);

    /**
     * Converts a set of statements into cache items (i.e. key + statement).
     *
     * @param statements  A number of statements.
     * @param factory     The storage factory using which the cache keys should be generated.
     * @param pItems      Pointer to vector where the items will be back inserted.
     *
     * @return  Whether the conversion was successful.
     */
    static bool get_cache_items(const Statements& statements,
                                const StorageFactory& factory,
                                CacheItems* pItems);

    /**
     * Converts statements from a stream into cache items (i.e. key + GWBUF).
     *
     * @param statements  A number of statements.
     * @param n_items     How many items should be returned.
     * @param factory     The storage factory using which the cache keys should be generated.
     * @param pItems      Pointer to vector where the items will be back inserted.
     *
     * @return  Whether reading and conversion was successful, not whether @n_items
     *          items were returned.
     */
    static bool get_cache_items(std::istream& in,
                                size_t n_items,
                                const StorageFactory& factory,
                                CacheItems* pItems);

    /**
     * Deletes the GWBUFs, and empties the vector.
     *
     * @param cache_items  The vector to be cleared.
     */
    static void clear_cache_items(CacheItems& cache_items);

    static int combine_rvs(int rv1, int rv2)
    {
        return ((rv1 == EXIT_FAILURE) || (rv2 == EXIT_FAILURE)) ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    static int combine_rvs(int rv1, int rv2, int rv3)
    {
        return combine_rvs(rv1, combine_rvs(rv2, rv3));
    }

    static int combine_rvs(int rv1, int rv2, int rv3, int rv4)
    {
        return combine_rvs(rv1, combine_rvs(rv2, rv3, rv4));
    }

    static int combine_rvs(int rv1, int rv2, int rv3, int rv4, int rv5)
    {
        return combine_rvs(rv1, combine_rvs(rv2, rv3, rv4, rv5));
    }

protected:
    /**
     * Constructor
     *
     * @param pOut  Pointer to stream to be used for (user) output. Must remain
     *              valid for the lifetime of the Tester instance.
     */
    Tester(std::ostream* pOut);

    /**
     * The stream to be used for (user) output.
     *
     * @return A stream.
     */
    std::ostream& out() const
    {
        return m_out;
    }

    /**
     * Execute a specific number of tasks in as many threads.
     *
     * @param out        The stream to be used for (user) output.
     * @param n_seconds  How many seconds the tasks should run.
     * @param tasks      Vector of tasks.
     *
     * @return EXIT_SUCCESS if each task returned EXIT_SUCCESS, otherwise EXIT_FAILURE.
     */
    static int execute(std::ostream& out, size_t n_seconds, const Tasks& tasks);

private:
    std::ostream& m_out;
};
