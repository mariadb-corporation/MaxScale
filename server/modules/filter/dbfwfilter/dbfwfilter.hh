#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
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
#include <tr1/memory>
#include <tr1/unordered_map>

#include <maxscale/filter.h>
#include <maxscale/query_classifier.h>
#include <maxscale/spinlock.h>

#include "dbfwfilter.h"

/**
 * Rule types
 */
typedef enum
{
    RT_UNDEFINED = 0x00, /*< Undefined rule */
    RT_COLUMN, /*<  Column name rule*/
    RT_FUNCTION, /*<  Function name rule*/
    RT_USES_FUNCTION, /*<  Function usage rule*/
    RT_THROTTLE, /*< Query speed rule */
    RT_PERMISSION, /*< Simple denying rule */
    RT_WILDCARD, /*< Wildcard denial rule */
    RT_REGEX, /*< Regex matching rule */
    RT_CLAUSE /*< WHERE-clause requirement rule */
} ruletype_t;

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
 * Linked list of strings.
 */
typedef struct strlink_t
{
    struct strlink_t *next;     /*< Next node in the list */
    char*             value;    /*< Value of the current node */
} STRLINK;

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
typedef struct queryspeed_t
{
    time_t               first_query; /*< Time when the first query occurred */
    time_t               triggered; /*< Time when the limit was exceeded */
    int                  period; /*< Measurement interval in seconds */
    int                  cooldown; /*< Time the user is denied access for */
    int                  count; /*< Number of queries done */
    int                  limit; /*< Maximum number of queries */
    long                 id;    /*< Unique id of the rule */
    bool                 active; /*< If the rule has been triggered */
} QUERYSPEED;

/**
 * The Firewall filter instance.
 */
typedef struct
{
    enum fw_actions action;     /*< Default operation mode, defaults to deny */
    int             log_match;  /*< Log matching and/or non-matching queries */
    SPINLOCK        lock;       /*< Instance spinlock */
    int             idgen;      /*< UID generator */
    char           *rulefile;   /*< Path to the rule file */
    int             rule_version; /*< Latest rule file version, incremented on reload */
} FW_INSTANCE;

/**
 * The session structure for Firewall filter.
 */
typedef struct
{
    MXS_SESSION   *session;      /*< Client session structure */
    char          *errmsg;       /*< Rule specific error message */
    QUERYSPEED    *query_speed;  /*< How fast the user has executed queries */
    MXS_DOWNSTREAM down;         /*< Next object in the downstream chain */
    MXS_UPSTREAM   up;           /*< Next object in the upstream chain */
} FW_SESSION;

/** Typedef for a list of strings */
typedef std::list<std::string> ValueList;

/** Temporary typedef for SRule */
class Rule;
typedef std::tr1::shared_ptr<Rule> SRule;

/** Helper function for strdup'ing in printf style */
char* create_error(const char* format, ...);

/**
 * Check if a rule matches
 */
bool rule_matches(FW_INSTANCE* my_instance, FW_SESSION* my_session,
                  GWBUF *queue, SRule rule, char* query);
bool rule_is_active(SRule rule);