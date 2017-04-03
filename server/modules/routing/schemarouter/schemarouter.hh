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

/**
 * @file schemarouter.hh - Common schemarouter definitions
 */

#define MXS_MODULE_NAME "schemarouter"

#include <maxscale/cdefs.h>

#include <limits>
#include <list>
#include <set>
#include <string>
#include <tr1/memory>

#include <maxscale/buffer.hh>
#include <maxscale/pcre2.h>
#include <maxscale/service.h>

#include "session_command.hh"

using std::list;
using std::set;
using std::string;
using std::tr1::shared_ptr;

using maxscale::Buffer;

/**
 * The state of the backend server reference
 */
enum bref_state
{
    BREF_IN_USE           = 0x01,
    BREF_WAITING_RESULT   = 0x02, /**< for session commands only */
    BREF_QUERY_ACTIVE     = 0x04, /**< for other queries */
    BREF_CLOSED           = 0x08,
    BREF_DB_MAPPED        = 0x10
};

namespace schemarouter
{
/**
 * Configuration values
 */
struct Config
{
    double            refresh_min_interval; /**< Minimum required interval between
                                             * refreshes of databases */
    bool              refresh_databases;    /**< Are databases refreshed when
                                             * they are not found in the hashtable */
    bool              debug;                /**< Enable verbose debug messages to clients */
    pcre2_code*       ignore_regex;         /**< Regular expression used to ignore databases */
    pcre2_match_data* ignore_match_data;    /**< Match data for @c ignore_regex */
    set<string>       ignored_dbs;          /**< Set of ignored databases */

    Config():
        refresh_min_interval(0.0),
        refresh_databases(false),
        debug(false),
        ignore_regex(NULL),
        ignore_match_data(NULL)
    {
    }
};

/**
 * Router statistics
 */
struct Stats
{
    int    n_queries;        /*< Number of queries forwarded    */
    int    n_sescmd;         /*< Number of session commands */
    int    longest_sescmd;   /*< Longest chain of stored session commands */
    int    n_hist_exceeded;  /*< Number of sessions that exceeded session
                              * command history limit */
    int    sessions;         /*< Number of sessions */
    int    shmap_cache_hit;  /*< Shard map was found from the cache */
    int    shmap_cache_miss; /*< No shard map found from the cache */
    double ses_longest;      /*< Longest session */
    double ses_shortest;     /*< Shortest session */
    double ses_average;      /*< Average session length */

    Stats():
        n_queries(0),
        n_sescmd(0),
        longest_sescmd(0),
        n_hist_exceeded(0),
        sessions(0),
        shmap_cache_hit(0),
        shmap_cache_miss(0),
        ses_longest(0.0),
        ses_shortest(std::numeric_limits<double>::max()),
        ses_average(0.0)
    {
    }
};

/**
 * Reference to BACKEND.
 *
 * Owned by router client session.
 */
class Backend
{
public:
    /**
     * @brief Create new Backend
     *
     * @param ref Server reference used by this backend
     */
    Backend(SERVER_REF *ref);

    ~Backend();

    /**
     * @brief Execute the next session command
     *
     * @return True if the command was executed successfully
     */
    bool execute_sescmd();

    /**
     * @brief Clear state
     *
     * @param state State to clear
     */
    void clear_state(enum bref_state state);

    /**
     * @brief Set state
     *
     * @param state State to set
     */
    void set_state(enum bref_state state);

    /**
     * @brief Get pointer to server reference
     *
     * @return Pointer to server reference
     */
    SERVER_REF* backend() const;

    /**
     * @brief Create a new connection
     *
     * @param session The session to which the connection is linked
     *
     * @return True if connection was successfully created
     */
    bool connect(MXS_SESSION* session);

    /**
     * @brief Close the backend
     *
     * This will close all active connections created by the backend.
     */
    void close();

    /**
     * @brief Get a pointer to the internal DCB
     *
     * @return Pointer to internal DCB
     */
    DCB* dcb() const;

    /**
     * @brief Write data to the backend server
     *
     * @param buffer Buffer containing the data to write
     *
     * @return True if data was written successfully
     */
    bool write(GWBUF* buffer);

    /**
     * @brief Store a command
     *
     * The command is stored and executed once the session can execute
     * the next command.
     *
     * @param buffer Buffer to store
     */
    void store_command(GWBUF* buffer);

    /**
     * @brief Write the stored command to the backend server
     *
     * @return True if command was written successfully
     */
    bool write_stored_command();

    /**
     * @brief Check if backend is in use
     *
     * @return True if backend is in use
     */
    bool in_use() const;

    /**
     * @brief Check if backend is waiting for a result
     *
     * @return True if backend is waiting for a result
     */
    bool is_waiting_result() const;

    /**
     * @brief Check if a query is active
     *
     * @return True if a query is active
     */
    bool is_query_active() const;

    /**
     * @brief Check if the backend is closed
     *
     * @return True if the backend is closed
     */
    bool is_closed() const;

    /**
     * @brief Check if the backend has been mapped
     *
     * @return True if the backend has been mapped
     */
    bool is_mapped() const;

private:
    bool               m_closed;           /**< True if a connection has been opened and closed */
    SERVER_REF*        m_backend;          /**< Backend server */
    DCB*               m_dcb;              /**< Backend DCB */

public:
    GWBUF*             m_map_queue;
    bool               m_mapped;           /**< Whether the backend has been mapped */
    int                m_num_mapping_eof;
    int                m_num_result_wait;  /**< Number of not yet received results */
    Buffer             m_pending_cmd;      /**< Pending commands */
    int                m_state;            /**< State of the backend */
    SessionCommandList m_session_commands; /**< List of session commands that are
                                            * to be executed on this backend server */
};

typedef shared_ptr<Backend> SBackend;
typedef list<SBackend> BackendList;

}
