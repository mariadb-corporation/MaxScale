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

// TODO: Move these as member functions, currently they operate on iterators
#define BREF_IS_NOT_USED(s)         ((s)->m_state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)           ((s)->m_state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s)   ((s)->m_num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)     ((s)->m_state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)           ((s)->m_state & BREF_CLOSED)
#define BREF_IS_MAPPED(s)           ((s)->m_mapped)

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
    Backend(SERVER_REF *ref);
    ~Backend();
    bool execute_sescmd();
    void clear_state(enum bref_state state);
    void set_state(enum bref_state state);
    SERVER_REF* backend() const;
    bool connect(MXS_SESSION*);
    void close();
    DCB* dcb() const;
    bool write(GWBUF* buffer);
    void store_command(GWBUF* buffer);
    bool write_stored_command();

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
