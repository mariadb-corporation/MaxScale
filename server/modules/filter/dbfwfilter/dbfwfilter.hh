/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#if !defined (MXS_MODULE_NAME)
#define MXS_MODULE_NAME "dbfwfilter"
#endif
#include <maxscale/ccdefs.hh>

#include <time.h>

#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <maxscale/filter.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>

#include "dbfwconfig.hh"
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
    }
}

/** Maximum length of the match/nomatch messages */
#define FW_MAX_SQL_LEN 400

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
    QuerySpeed()
        : first_query(0)
        , triggered(0)
        , count(0)
        , active(false)
    {
    }

    time_t first_query; /*< Time when the first query occurred */
    time_t triggered;   /*< Time when the limit was exceeded */
    int    count;       /*< Number of queries done */
    bool   active;      /*< If the rule has been triggered */
};

class Dbfw;
class User;
typedef std::shared_ptr<User> SUser;

/**
 * The session structure for Firewall filter.
 */
class DbfwSession : public mxs::FilterSession
{
    DbfwSession(const DbfwSession&);
    DbfwSession& operator=(const DbfwSession&);

public:
    DbfwSession(Dbfw* instance, MXS_SESSION* session, SERVICE* service);
    ~DbfwSession();

    void        set_error(const char* error);
    std::string get_error() const;
    void        clear_error();
    int         send_error();

    std::string user() const;
    std::string remote() const;

    bool        routeQuery(GWBUF* query) override;
    QuerySpeed* query_speed();      // TODO: Remove this, it exposes internals to a Rule
    fw_actions  get_action() const;

private:
    Dbfw*        m_instance;    /*< Router instance */
    MXS_SESSION* m_session;     /*< Client session structure */
    std::string  m_error;       /*< Rule specific error message */
    QuerySpeed   m_qs;          /*< How fast the user has executed queries */
};

/**
 * The Firewall filter instance.
 */
class Dbfw : public mxs::Filter
{
    Dbfw(const Dbfw&);
    Dbfw& operator=(const Dbfw&);

public:
    Dbfw(const char* zName);
    ~Dbfw();

    /**
     * Create a new Dbfw instance
     *
     * @return New instance or NULL on error
     */
    static Dbfw* create(const char* zName);

    /**
     * Create a new filter session
     *
     * @param session Session object
     *
     * @return New session or NULL on error
     */
    DbfwSession* newSession(MXS_SESSION* session, SERVICE* service) override;

    /**
     * Get the action mode of this instance
     *
     * @return The action mode
     */
    fw_actions get_action() const;

    /**
     * Should strings be treated as fields?
     *
     * @return True, if they should, false otherwise.
     */
    bool treat_string_as_field() const
    {
        return m_config.treat_string_as_field;
    }

    /**
     * Should string args be treated as fields?
     *
     * @return True, if they should, false otherwise.
     */
    bool treat_string_arg_as_field() const
    {
        return m_config.treat_string_arg_as_field;
    }

    /**
     * Whether unsupported SQL is an error
     *
     * @return True if unsupported SQL is an error
     */
    bool strict() const
    {
        return m_config.strict;
    }

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

    /** Diagnostic routine */
    json_t* diagnostics() const override;

    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_STMT_INPUT;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    const DbfwConfig& config() const
    {
        return m_config;
    }

    bool post_configure();

private:
    DbfwConfig         m_config;
    int                m_log_match = 0; /*< Log matching and/or non-matching queries */
    mutable std::mutex m_lock;          /*< Instance spinlock */
    int                m_version;       /*< Latest rule file version, incremented on reload */

    bool do_reload_rules(std::string filename);
};

/** Typedef for a list of strings */
typedef std::list<std::string> ValueList;

/** Temporary typedef for SRule */
class Rule;
typedef std::shared_ptr<Rule> SRule;

/** Helper function for strdup'ing in printf style */
char* create_error(const char* format, ...) __attribute__ ((nonnull));

/**
 * Check if a rule matches
 */
bool rule_matches(Dbfw* my_instance,
                  DbfwSession* my_session,
                  GWBUF* queue,
                  SRule rule,
                  char* query);
bool rule_is_active(SRule rule);
