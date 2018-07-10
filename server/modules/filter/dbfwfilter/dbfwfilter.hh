#pragma once
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

#define MXS_MODULE_NAME "dbfwfilter"
#include <maxscale/cppdefs.hh>

#include <time.h>
#include <string>
#include <list>
#include <vector>
#include <memory>
#include <unordered_map>

#include <maxscale/filter.hh>
#include <maxscale/query_classifier.h>
#include <maxscale/spinlock.h>

#include "dbfwfilter.h"

/**
 * What operator a rule should apply to.
 *
 * Note that each operator is represented by a unique bit, so that they
 * can be combined as a bitmask, while query_op_t enumeration of the query
 * classifier consists of a sequence of unique numbers.
 */
typedef enum fw_op
{
    FW_OP_UNDEFINED = 0,

    // NOTE: If you add something here, check the 'qc_op_to_fw_op' function below.
    FW_OP_ALTER     = (1 << 0),
    FW_OP_CHANGE_DB = (1 << 1),
    FW_OP_CREATE    = (1 << 2),
    FW_OP_DELETE    = (1 << 3),
    FW_OP_DROP      = (1 << 4),
    FW_OP_GRANT     = (1 << 5),
    FW_OP_INSERT    = (1 << 6),
    FW_OP_LOAD      = (1 << 7),
    FW_OP_REVOKE    = (1 << 8),
    FW_OP_SELECT    = (1 << 9),
    FW_OP_UPDATE    = (1 << 10),
} fw_op_t;

/**
 * Convert a qc_query_op_t to the equivalent fw_op_t.
 *
 * @param op A query classifier operator.
 *
 * @return The corresponding bit value.
 */
static inline fw_op_t qc_op_to_fw_op(qc_query_op_t op)
{
    switch (op)
    {
    case QUERY_OP_ALTER:
        return FW_OP_ALTER;

    case QUERY_OP_CHANGE_DB:
        return FW_OP_CHANGE_DB;

    case QUERY_OP_CREATE:
        return FW_OP_CREATE;

    case QUERY_OP_DELETE:
        return FW_OP_DELETE;

    case QUERY_OP_DROP:
        return FW_OP_DROP;

    case QUERY_OP_GRANT:
        return FW_OP_GRANT;

    case QUERY_OP_INSERT:
        return FW_OP_INSERT;

    case QUERY_OP_LOAD_LOCAL:
    case QUERY_OP_LOAD:
        return FW_OP_LOAD;

    case QUERY_OP_REVOKE:
        return FW_OP_REVOKE;

    case QUERY_OP_SELECT:
        return FW_OP_SELECT;

    case QUERY_OP_UPDATE:
        return FW_OP_UPDATE;

    default:
        return FW_OP_UNDEFINED;
    };
}

/**
 * Possible actions to take when the query matches a rule
 */
enum fw_actions
{
    FW_ACTION_ALLOW,
    FW_ACTION_BLOCK,
    FW_ACTION_IGNORE
};

/**
 * Logging options for matched queries
 */
#define FW_LOG_NONE         0x00
#define FW_LOG_MATCH        0x01
#define FW_LOG_NO_MATCH     0x02

/** Maximum length of the match/nomatch messages */
#define FW_MAX_SQL_LEN      400

/**
 * A structure defining a range of time
 */
typedef struct timerange_t
{
    struct timerange_t* next;   /*< Next node in the list */
    struct tm           start;  /*< Start of the time range */
    struct tm           end;    /*< End of the time range */
} TIMERANGE;

/**
 * Query speed measurement and limitation structure
 */
struct QuerySpeed
{
    QuerySpeed():
        first_query(0),
        triggered(0),
        count(0),
        active(false)
    {
    }

    time_t               first_query; /*< Time when the first query occurred */
    time_t               triggered; /*< Time when the limit was exceeded */
    int                  count; /*< Number of queries done */
    bool                 active; /*< If the rule has been triggered */
};

class Dbfw;
class User;
typedef std::shared_ptr<User> SUser;

/**
 * The session structure for Firewall filter.
 */
class DbfwSession: public mxs::FilterSession
{
    DbfwSession(const DbfwSession&);
    DbfwSession& operator=(const DbfwSession&);

public:
    DbfwSession(Dbfw* instance, MXS_SESSION* session);
    ~DbfwSession();

    void set_error(const char* error);
    std::string get_error() const;
    void clear_error();
    int send_error();

    std::string user() const;
    std::string remote() const;

    int routeQuery(GWBUF* query);
    QuerySpeed* query_speed(); // TODO: Remove this, it exposes internals to a Rule
    fw_actions get_action() const;

private:
    Dbfw          *m_instance; /*< Router instance */
    MXS_SESSION   *m_session;  /*< Client session structure */
    std::string    m_error;    /*< Rule specific error message */
    QuerySpeed     m_qs;       /*< How fast the user has executed queries */
};

/**
 * The Firewall filter instance.
 */
class Dbfw: public mxs::Filter<Dbfw, DbfwSession>
{
    Dbfw(const Dbfw&);
    Dbfw& operator=(const Dbfw&);

public:
    ~Dbfw();

    /**
     * Create a new Dbfw instance
     *
     * @param params Configuration parameters for this instance
     *
     * @return New instance or NULL on error
     */
    static Dbfw* create(const char* zName, MXS_CONFIG_PARAMETER* ppParams);

    /**
     * Create a new filter session
     *
     * @param session Session object
     *
     * @return New session or NULL on error
     */
    DbfwSession* newSession(MXS_SESSION* session);

    /**
     * Get the action mode of this instance
     *
     * @return The action mode
     */
    fw_actions get_action() const;

    /**
     * Get logging option bitmask
     *
     * @return the logging option bitmask
     */
    int get_log_bitmask() const;

    /**
     * Get the current rule file
     *
     * @return The current rule file
     */
    std::string get_rule_file() const;

    /**
     * Get current rule version number
     *
     * @return The current rule version number
     */
    int get_rule_version() const;

    /**
     * Reload rules from a file
     *
     * @param filename File to reload rules from
     *
     * @return True if rules were reloaded successfully, false on error. If an
     *         error occurs, it is stored in the modulecmd error system.
     */
    bool reload_rules(std::string filename);

    /** Diagnostic routines */
    void diagnostics(DCB *dcb) const;
    json_t* diagnostics_json() const;

    uint64_t getCapabilities() const
    {
        return RCAP_TYPE_NONE;
    }

private:
    fw_actions  m_action;    /*< Default operation mode, defaults to deny */
    int         m_log_match; /*< Log matching and/or non-matching queries */
    SPINLOCK    m_lock;      /*< Instance spinlock */
    std::string m_filename;  /*< Path to the rule file */
    int         m_version;   /*< Latest rule file version, incremented on reload */

    Dbfw(MXS_CONFIG_PARAMETER* param);
    bool do_reload_rules(std::string filename);
};

/** Typedef for a list of strings */
typedef std::list<std::string> ValueList;

/** Temporary typedef for SRule */
class Rule;
typedef std::shared_ptr<Rule> SRule;

/** Helper function for strdup'ing in printf style */
char* create_error(const char* format, ...);

/**
 * Check if a rule matches
 */
bool rule_matches(Dbfw* my_instance, DbfwSession* my_session,
                  GWBUF *queue, SRule rule, char* query);
bool rule_is_active(SRule rule);
