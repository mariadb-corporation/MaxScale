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
 * @file dbfwfilter.c
 * @author Markus Mäkelä
 * @date 13.2.2015
 * @version 1.0.0
 * @section secDesc Firewall Filter
 *
 * A filter that acts as a firewall, denying queries that do not meet a set of rules.
 *
 * Filter configuration parameters:
 *@code{.unparsed}
 *      rules=<path to file>            Location of the rule file
 *@endcode
 * Rules are defined in a separate rule file that lists all the rules and the users to whom the
 * rules are applied.
 * Rules follow a simple syntax that denies the queries that meet the requirements of the rules.
 * For example, to define a rule denying users from accessing the column 'salary' between
 * the times 15:00 and 17:00, the following rule is to be configured into the configuration file:
 *@code{.unparsed}
 *      rule block_salary deny columns salary at_times 15:00:00-17:00:00
 *@endcode
 * The users are matched by username and network address. Wildcard values can be provided by
 * using the '%' character.
 * For example, to apply this rule to users John, connecting from any address
 * that starts with the octets 198.168.%, and Jane, connecting from the address 192.168.0.1:
 *@code{.unparsed}
 *      users John@192.168.% Jane@192.168.0.1 match any rules block_salary
 *@endcode
 *
 * The 'match' keyword controls the way rules are matched. If it is set to
 * 'any' the first active rule that is triggered will cause the query to be denied.
 * If it is set to 'all' all the active rules need to match before the query is denied.
 *
 * @subsection secRule Rule syntax
 * This is the syntax used when defining rules.
 *@code{.unparsed}
 * rule NAME deny [wildcard | columns VALUE ... | regex REGEX |
 *           limit_queries COUNT TIMEPERIOD HOLDOFF | no_where_clause] [at_times VALUE...]
 *           [on_queries [select|update|insert|delete]]
 *@endcode
 * @subsection secUser User syntax
 * This is the syntax used when linking users to rules. It takes one or more
 * combinations of username and network, either the value any or all,
 * depending on how you want to match the rules, and one or more rule names.
 *@code{.unparsed}
 * users NAME ... match [any|all|strict_all] rules RULE ...
 *@endcode
 */

#define MXS_MODULE_NAME "dbfwfilter"
#include <maxscale/cdefs.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <regex.h>
#include <stdlib.h>

#include <maxscale/filter.h>
#include <maxscale/atomic.h>
#include <maxscale/modulecmd.h>
#include <maxscale/modutil.h>
#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/platform.h>
#include <maxscale/spinlock.h>
#include <maxscale/thread.h>
#include <maxscale/pcre2.h>
#include <maxscale/alloc.h>

#include "dbfwfilter.h"
#include "ruleparser.yy.h"
#include "lex.yy.h"

/** Older versions of Bison don't include the parsing function in the header */
#ifndef dbfw_yyparse
int dbfw_yyparse(void*);
#endif

/*
 * The filter entry points
 */
static MXS_FILTER *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *);
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static uint64_t getCapabilities(MXS_FILTER* instance);

/**
 * Rule types
 */
typedef enum
{
    RT_UNDEFINED = 0x00, /*< Undefined rule */
    RT_COLUMN, /*<  Column name rule*/
    RT_FUNCTION, /*<  Function name rule*/
    RT_THROTTLE, /*< Query speed rule */
    RT_PERMISSION, /*< Simple denying rule */
    RT_WILDCARD, /*< Wildcard denial rule */
    RT_REGEX, /*< Regex matching rule */
    RT_CLAUSE /*< WHERE-clause requirement rule */
} ruletype_t;

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

const char* rule_names[] =
{
    "UNDEFINED",
    "COLUMN",
    "THROTTLE",
    "PERMISSION",
    "WILDCARD",
    "REGEX",
    "CLAUSE"
};

const int rule_names_len = sizeof(rule_names) / sizeof(char**);

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
 * A structure used to identify individual rules and to store their contents
 *
 * Each type of rule has different requirements that are expressed as void pointers.
 * This allows to match an arbitrary set of rules against a user.
 */
typedef struct rule_t
{
    void*          data;        /*< Actual implementation of the rule */
    char*          name;        /*< Name of the rule */
    ruletype_t     type;        /*< Type of the rule */
    qc_query_op_t  on_queries;  /*< Types of queries to inspect */
    int            times_matched; /*< Number of times this rule has been matched */
    TIMERANGE*     active;      /*< List of times when this rule is active */
    struct rule_t *next;
} RULE;

/**
 * A set of rules that the filter follows
 */
typedef struct rulebook_t
{
    RULE*              rule;    /*< The rule structure */
    struct rulebook_t* next;    /*< The next rule in the book */
} RULE_BOOK;

thread_local int        thr_rule_version = 0;
thread_local RULE      *thr_rules = NULL;
thread_local HASHTABLE *thr_users = NULL;

/**
 * A temporary template structure used in the creation of actual users.
 * This is also used to link the user definitions with the rules.
 * @see struct user_t
 */
typedef struct user_template
{
    char                 *name;
    enum match_type       type; /** Matching type */
    STRLINK              *rulenames; /** names of the rules */
    struct user_template *next;
} user_template_t;

/**
 * A user definition
 */
typedef struct user_t
{
    char*       name;           /*< Name of the user */
    SPINLOCK    lock;           /*< User spinlock */
    QUERYSPEED* qs_limit;       /*< The query speed structure unique to this user */
    RULE_BOOK*  rules_or;       /*< If any of these rules match the action is triggered */
    RULE_BOOK*  rules_and;      /*< All of these rules must match for the action to trigger */
    RULE_BOOK*  rules_strict_and; /*< rules that skip the rest of the rules if one of them
                                   * fails. This is only for rules paired with 'match strict_all'. */
} DBFW_USER;

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

bool parse_at_times(const char** tok, char** saveptr, RULE* ruledef);
bool parse_limit_queries(FW_INSTANCE* instance, RULE* ruledef, const char* rule, char** saveptr);
static void rule_free_all(RULE* rule);
static bool process_rule_file(const char* filename, RULE** rules, HASHTABLE **users);
bool replace_rules(FW_INSTANCE* instance);

static void print_rule(RULE *rules, char *dest)
{
    int type = 0;

    if ((int)rules->type > 0 && (int)rules->type < rule_names_len)
    {
        type = (int)rules->type;
    }

    sprintf(dest, "%s, %s, %d",
            rules->name,
            rule_names[type],
            rules->times_matched);
}

/**
 * Push a string onto a string stack
 * @param head Head of the stack
 * @param value value to add
 * @return New top of the stack or NULL if memory allocation fails
 */
static STRLINK* strlink_push(STRLINK* head, const char* value)
{
    STRLINK* link = MXS_MALLOC(sizeof(STRLINK));

    if (link && (link->value = MXS_STRDUP(value)))
    {
        link->next = head;
    }
    else
    {
        MXS_FREE(link);
        link = NULL;
    }
    return link;
}

/**
 * Pop a string off of a string stack
 * @param head Head of the stack
 * @return New head of the stack or NULL if stack is empty
 */
static STRLINK* strlink_pop(STRLINK* head)
{
    if (head)
    {
        STRLINK* next = head->next;
        MXS_FREE(head->value);
        MXS_FREE(head);
        return next;
    }
    return NULL;
}

/**
 * Free a string stack
 * @param head Head of the stack
 */
static void strlink_free(STRLINK* head)
{
    while (head)
    {
        STRLINK* tmp = head;
        head = head->next;
        MXS_FREE(tmp->value);
        MXS_FREE(tmp);
    }
}

/**
 * Clone a string stack. This function reverses the order of the stack.
 * @param head Head of the stack to be cloned
 * @return Clone of the head or NULL if memory allocation failed
 */
static STRLINK* strlink_reverse_clone(STRLINK* head)
{
    STRLINK* clone = NULL;
    while (head)
    {
        STRLINK *tmp = strlink_push(clone, head->value);
        if (tmp)
        {
            clone = tmp;
        }
        else
        {
            strlink_free(clone);
            clone = NULL;
            break;
        }
        head = head->next;
    }
    return clone;
}

/**
 * Add a rule to a rulebook
 * @param head
 * @param rule
 * @return
 */
static RULE_BOOK* rulebook_push(RULE_BOOK *head, RULE *rule)
{
    RULE_BOOK *rval = MXS_MALLOC(sizeof(RULE_BOOK));

    if (rval)
    {
        rval->rule = rule;
        rval->next = head;
    }
    return rval;
}

static void* rulebook_clone(void* fval)
{

    RULE_BOOK *rule = NULL,
               *ptr = (RULE_BOOK*) fval;


    while (ptr)
    {
        RULE_BOOK* tmp = (RULE_BOOK*) MXS_MALLOC(sizeof(RULE_BOOK));
        MXS_ABORT_IF_NULL(tmp);
        tmp->next = rule;
        tmp->rule = ptr->rule;
        rule = tmp;
        ptr = ptr->next;
    }

    return (void*) rule;
}

static void* rulebook_free(void* fval)
{
    RULE_BOOK *ptr = (RULE_BOOK*) fval;
    while (ptr)
    {
        RULE_BOOK *tmp = ptr;
        ptr = ptr->next;
        MXS_FREE(tmp);
    }
    return NULL;
}

static void dbfw_user_free(void* fval)
{
    DBFW_USER* value = (DBFW_USER*) fval;

    rulebook_free(value->rules_and);
    rulebook_free(value->rules_or);
    rulebook_free(value->rules_strict_and);
    MXS_FREE(value->qs_limit);
    MXS_FREE(value->name);
    MXS_FREE(value);
}

HASHTABLE *dbfw_userlist_create()
{
    HASHTABLE *ht = hashtable_alloc(100, hashtable_item_strhash, hashtable_item_strcmp);

    if (ht)
    {
        hashtable_memory_fns(ht, hashtable_item_strdup, NULL, hashtable_item_free, dbfw_user_free);
    }

    return ht;
}

/**
 * Parses a string that contains an IP address and converts the last octet to '%'.
 * This modifies the string passed as the parameter.
 * @param str String to parse
 * @return Pointer to modified string or NULL if an error occurred or the string can't
 *         be made any less specific
 */
char* next_ip_class(char* str)
{
    assert(str != NULL);

    /**The least specific form is reached*/
    if (*str == '%')
    {
        return NULL;
    }

    char* ptr = strchr(str, '\0');

    if (ptr == NULL)
    {
        return NULL;
    }

    while (ptr > str)
    {
        ptr--;
        if (*ptr == '.' && *(ptr + 1) != '%')
        {
            break;
        }
    }

    if (ptr == str)
    {
        *ptr++ = '%';
        *ptr = '\0';
        return str;
    }

    *++ptr = '%';
    *++ptr = '\0';


    return str;
}

/**
 * Parses the string for the types of queries this rule should be applied to.
 * @param str String to parse
 * @param rule Pointer to a rule
 * @return True if the string was parses successfully, false if an error occurred
 */
bool parse_querytypes(const char* str, RULE* rule)
{
    char buffer[512];
    bool done = false;
    rule->on_queries = 0;
    const char *ptr = str;
    char *dest = buffer;

    while (ptr - str < 512)
    {
        if (*ptr == '|' || *ptr == ' ' || (done = *ptr == '\0'))
        {
            *dest = '\0';
            if (strcmp(buffer, "select") == 0)
            {
                rule->on_queries |= QUERY_OP_SELECT;
            }
            else if (strcmp(buffer, "insert") == 0)
            {
                rule->on_queries |= QUERY_OP_INSERT;
            }
            else if (strcmp(buffer, "update") == 0)
            {
                rule->on_queries |= QUERY_OP_UPDATE;
            }
            else if (strcmp(buffer, "delete") == 0)
            {
                rule->on_queries |= QUERY_OP_DELETE;
            }
            else if (strcmp(buffer, "use") == 0)
            {
                rule->on_queries |= QUERY_OP_CHANGE_DB;
            }
            else if (strcmp(buffer, "grant") == 0)
            {
                rule->on_queries |= QUERY_OP_GRANT;
            }
            else if (strcmp(buffer, "revoke") == 0)
            {
                rule->on_queries |= QUERY_OP_REVOKE;
            }
            else if (strcmp(buffer, "drop") == 0)
            {
                rule->on_queries |= QUERY_OP_DROP;
            }
            else if (strcmp(buffer, "create") == 0)
            {
                rule->on_queries |= QUERY_OP_CREATE;
            }
            else if (strcmp(buffer, "alter") == 0)
            {
                rule->on_queries |= QUERY_OP_ALTER;
            }
            else if (strcmp(buffer, "load") == 0)
            {
                rule->on_queries |= QUERY_OP_LOAD;
            }

            if (done)
            {
                return true;
            }

            dest = buffer;
            ptr++;
        }
        else
        {
            *dest++ = *ptr++;
        }
    }
    return false;
}

/**
 * Checks whether a null-terminated string contains two ISO-8601 compliant times separated
 * by a single dash.
 * @param str String to check
 * @return True if the string is valid
 */
bool check_time(const char* str)
{
    assert(str != NULL);

    const char* ptr = str;
    int colons = 0, numbers = 0, dashes = 0;
    while (*ptr && ptr - str < 18)
    {
        if (isdigit(*ptr))
        {
            numbers++;
        }
        else if (*ptr == ':')
        {
            colons++;
        }
        else if (*ptr == '-')
        {
            dashes++;
        }
        ptr++;
    }
    return numbers == 12 && colons == 4 && dashes == 1;
}


#ifdef SS_DEBUG
#define CHK_TIMES(t) ss_dassert(t->tm_sec > -1 && t->tm_sec < 62        \
                                && t->tm_min > -1 && t->tm_min < 60     \
                                && t->tm_hour > -1 && t->tm_hour < 24)
#else
#define CHK_TIMES(t)
#endif

#define IS_RVRS_TIME(tr) (mktime(&tr->end) < mktime(&tr->start))

/**
 * Parses a null-terminated string into a timerange defined by two ISO-8601 compliant
 * times separated by a single dash. The times are interpreted at one second precision
 * and follow the extended format by separating the hours, minutes and seconds with
 * semicolons.
 * @param str String to parse
 * @param instance FW_FILTER instance
 * @return If successful returns a pointer to the new TIMERANGE instance. If errors occurred or
 * the timerange was invalid, a NULL pointer is returned.
 */
static TIMERANGE* parse_time(const char* str)
{
    assert(str != NULL);

    char strbuf[strlen(str) + 1];
    char *separator;
    struct tm start, end;
    TIMERANGE* tr = NULL;

    memset(&start, 0, sizeof(start));
    memset(&end, 0, sizeof(end));
    strcpy(strbuf, str);

    if ((separator = strchr(strbuf, '-')))
    {
        *separator++ = '\0';
        if (strptime(strbuf, "%H:%M:%S", &start) &&
            strptime(separator, "%H:%M:%S", &end))
        {
            /** The time string was valid */
            CHK_TIMES((&start));
            CHK_TIMES((&end));

            tr = (TIMERANGE*) MXS_MALLOC(sizeof(TIMERANGE));

            if (tr)
            {
                tr->start = start;
                tr->end = end;
                tr->next = NULL;
            }
        }
    }
    return tr;
}

/**
 * Splits the reversed timerange into two.
 *@param tr A reversed timerange
 *@return If the timerange is reversed, returns a pointer to the new TIMERANGE
 *        otherwise returns a NULL pointer
 */
TIMERANGE* split_reverse_time(TIMERANGE* tr)
{
    TIMERANGE* tmp = NULL;

    tmp = (TIMERANGE*) MXS_CALLOC(1, sizeof(TIMERANGE));
    MXS_ABORT_IF_NULL(tmp);
    tmp->next = tr;
    tmp->start.tm_hour = 0;
    tmp->start.tm_min = 0;
    tmp->start.tm_sec = 0;
    tmp->end = tr->end;
    tr->end.tm_hour = 23;
    tr->end.tm_min = 59;
    tr->end.tm_sec = 59;
    return tmp;
}

bool dbfw_reload_rules(const MODULECMD_ARG *argv)
{
    bool rval = true;
    MXS_FILTER_DEF *filter = argv->argv[0].value.filter;
    FW_INSTANCE *inst = (FW_INSTANCE*)filter_def_get_instance(filter);

    if (modulecmd_arg_is_present(argv, 1))
    {
        /** We need to change the rule file */
        char *newname = MXS_STRDUP(argv->argv[1].value.string);

        if (newname)
        {
            spinlock_acquire(&inst->lock);

            char *oldname = inst->rulefile;
            inst->rulefile = newname;

            spinlock_release(&inst->lock);

            MXS_FREE(oldname);
        }
        else
        {
            modulecmd_set_error("Memory allocation failed");
            rval = false;
        }
    }

    spinlock_acquire(&inst->lock);
    char filename[strlen(inst->rulefile) + 1];
    strcpy(filename, inst->rulefile);
    spinlock_release(&inst->lock);

    RULE *rules = NULL;
    HASHTABLE *users = NULL;

    if (rval && access(filename, R_OK) == 0)
    {
        if (process_rule_file(filename, &rules, &users))
        {
            atomic_add(&inst->rule_version, 1);
            MXS_NOTICE("Reloaded rules from: %s", filename);
        }
        else
        {
            modulecmd_set_error("Failed to process rule file '%s'. See log "
                                "file for more details.", filename);
            rval = false;
        }
    }
    else
    {
        char err[MXS_STRERROR_BUFLEN];
        modulecmd_set_error("Failed to read rules at '%s': %d, %s", filename,
                            errno, strerror_r(errno, err, sizeof(err)));
        rval = false;
    }

    rule_free_all(rules);
    hashtable_free(users);

    return rval;
}

bool dbfw_show_rules(const MODULECMD_ARG *argv)
{
    DCB *dcb = argv->argv[0].value.dcb;
    MXS_FILTER_DEF *filter = argv->argv[1].value.filter;
    FW_INSTANCE *inst = (FW_INSTANCE*)filter_def_get_instance(filter);

    dcb_printf(dcb, "Rule, Type, Times Matched\n");

    if (!thr_rules || !thr_users)
    {
        if (!replace_rules(inst))
        {
            return 0;
        }
    }

    for (RULE *rule = thr_rules; rule; rule = rule->next)
    {
        char buf[strlen(rule->name) + 200]; // Some extra space
        print_rule(rule, buf);
        dcb_printf(dcb, "%s\n", buf);
    }

    return true;
}

static const MXS_ENUM_VALUE action_values[] =
{
    {"allow",  FW_ACTION_ALLOW},
    {"block",  FW_ACTION_BLOCK},
    {"ignore", FW_ACTION_IGNORE},
    {NULL}
};

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    modulecmd_arg_type_t args_rules_reload[] =
    {
        {MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Filter to reload"},
        {MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL, "Path to rule file"}
    };

    modulecmd_register_command(MXS_MODULE_NAME, "rules/reload", dbfw_reload_rules, 2, args_rules_reload);

    modulecmd_arg_type_t args_rules_show[] =
    {
        {MODULECMD_ARG_OUTPUT, "DCB where result is written"},
        {MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Filter to inspect"}
    };

    modulecmd_register_command(MXS_MODULE_NAME, "rules", dbfw_show_rules, 2, args_rules_show);

    static MXS_FILTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        setDownstream,
        NULL, // No setUpStream
        routeQuery,
        NULL, // No clientReply
        diagnostic,
        getCapabilities,
        NULL, // No destroyInstance
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "Firewall Filter",
        "V1.2.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                "rules",
                MXS_MODULE_PARAM_PATH,
                NULL,
                MXS_MODULE_OPT_REQUIRED | MXS_MODULE_OPT_PATH_R_OK
            },
            {
                "log_match",
                MXS_MODULE_PARAM_BOOL,
                "false"
            },
            {
                "log_no_match",
                MXS_MODULE_PARAM_BOOL,
                "false"
            },
            {
                "action",
                MXS_MODULE_PARAM_ENUM,
                "block",
                MXS_MODULE_OPT_ENUM_UNIQUE,
                action_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Free a TIMERANGE struct
 * @param tr pointer to a TIMERANGE struct
 */
void timerange_free(TIMERANGE* tr)
{
    TIMERANGE *node, *tmp;

    node = tr;

    while (node)
    {
        tmp = node;
        node = node->next;
        MXS_FREE(tmp);
    }
}

/**
 * Retrieve the quoted regex string from a rule definition and
 * return the unquoted version of it.
 * @param saved Pointer to the last stored position in the string
 * @return The unquoted string or NULL if the string was malformed
 */
char* get_regex_string(char** saved)
{
    char *start = NULL, *ptr = *saved;
    bool escaped = false, quoted = false;
    char delimiter = 0;
    while (*ptr != '\0')
    {
        if (!escaped)
        {
            if (!isspace(*ptr))
            {
                switch (*ptr)
                {
                case '\'':
                case '"':
                    if (quoted)
                    {
                        if (*ptr == delimiter)
                        {
                            *ptr = '\0';
                            *saved = ptr + 1;
                            return start;
                        }
                    }
                    else
                    {
                        delimiter = *ptr;
                        start = ptr + 1;
                        quoted = true;
                    }
                    break;
                case '\\':
                    escaped = true;
                    break;
                default:
                    break;
                }
            }
        }
        else
        {
            escaped = false;
        }
        ptr++;
    }

    if (quoted)
    {
        MXS_ERROR("Missing ending quote, found '%c' but no matching unescaped"
                  " one was found.", delimiter);
    }

    return NULL;
}

/**
 * Structure used to hold rules and users that are being parsed
 */
struct parser_stack
{
    RULE* rule;
    STRLINK* user;
    STRLINK* active_rules;
    enum match_type active_mode;
    user_template_t* templates;
};

/**
 * Report parsing errors
 * @param scanner Currently active scanner
 * @param error Error message
 */
void dbfw_yyerror(void* scanner, const char* error)
{
    MXS_ERROR("Error on line %d, %s: %s\n", dbfw_yyget_lineno(scanner),
              error, dbfw_yyget_text(scanner));
}

/**
 * @brief Find a rule by name
 *
 * @param rules List of all rules
 * @param name Name of the rule
 * @return Pointer to the rule or NULL if rule was not found
 */
static RULE* find_rule_by_name(RULE* rules, const char* name)
{
    while (rules)
    {
        if (strcmp(rules->name, name) == 0)
        {
            return rules;
        }
        rules = rules->next;
    }

    return NULL;
}

/**
 * Create a new rule
 *
 * The rule is created with the default type which will always match. The rule
 * is later specialized by the definition of the actual rule.
 * @param scanner Current scanner
 * @param name Name of the rule
 */
bool create_rule(void* scanner, const char* name)
{
    bool rval = false;
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t)scanner);
    ss_dassert(rstack);

    if (find_rule_by_name(rstack->rule, name) == NULL)
    {
        RULE *ruledef = MXS_MALLOC(sizeof(RULE));

        if (ruledef && (ruledef->name = MXS_STRDUP(name)))
        {
            ruledef->type = RT_PERMISSION;
            ruledef->on_queries = QUERY_OP_UNDEFINED;
            ruledef->next = rstack->rule;
            ruledef->active = NULL;
            ruledef->times_matched = 0;
            ruledef->data = NULL;
            rstack->rule = ruledef;
            rval = true;
        }
        else
        {
            MXS_FREE(ruledef);
        }
    }
    else
    {
        MXS_ERROR("Redefinition of rule '%s' on line %d.", name, dbfw_yyget_lineno(scanner));
    }

    return rval;
}

/**
 * Free a list of rules
 * @param rule Rules to free
 */
static void rule_free_all(RULE* rule)
{
    while (rule)
    {
        RULE *tmp = rule->next;
        if (rule->active)
        {
            timerange_free(rule->active);
        }

        switch (rule->type)
        {
        case RT_COLUMN:
        case RT_FUNCTION:
            strlink_free((STRLINK*) rule->data);
            break;

        case RT_THROTTLE:
            MXS_FREE(rule->data);
            break;

        case RT_REGEX:
            pcre2_code_free((pcre2_code*) rule->data);
            break;

        default:
            break;
        }

        MXS_FREE(rule->name);
        MXS_FREE(rule);
        rule = tmp;
    }
}

/**
 * Add a user to the current rule linking expression
 * @param scanner Current scanner
 * @param name Name of the user
 */
bool add_active_user(void* scanner, const char* name)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    STRLINK *tmp = strlink_push(rstack->user, name);

    if (tmp)
    {
        rstack->user = tmp;
    }

    return tmp != NULL;
}

/**
 * Add a rule to the current rule linking expression
 * @param scanner Current scanner
 * @param name Name of the rule
 */
bool add_active_rule(void* scanner, const char* name)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    STRLINK *tmp = strlink_push(rstack->active_rules, name);

    if (tmp)
    {
        rstack->active_rules = tmp;
    }

    return tmp != NULL;
}

/**
 * Add an optional at_times definition to the rule
 * @param scanner Current scanner
 * @param range two ISO-8601 compliant times separated by a single dash
 */
bool add_at_times_rule(void* scanner, const char* range)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    TIMERANGE* timerange = parse_time(range);
    ss_dassert(timerange);

    if (timerange)
    {
        timerange->next = rstack->rule->active;
        rstack->rule->active = timerange;
    }

    return timerange != NULL;
}

/**
 * Add an optional on_queries definition to the rule
 * @param scanner Current scanner
 * @param sql List of SQL operations separated by vertical bars
 */
void add_on_queries_rule(void* scanner, const char* sql)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    parse_querytypes(sql, rstack->rule);
}

/**
 * Link users and rules
 * @param scanner Current scanner
 */
bool create_user_templates(void* scanner)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    user_template_t* templates = NULL;
    STRLINK* user = rstack->user;

    while (user)
    {
        user_template_t* newtemp = MXS_MALLOC(sizeof(user_template_t));
        STRLINK* tmp;
        if (newtemp && (newtemp->name = MXS_STRDUP(user->value)) &&
            (newtemp->rulenames = strlink_reverse_clone(rstack->active_rules)))
        {
            newtemp->type = rstack->active_mode;
            newtemp->next = templates;
            templates = newtemp;
        }
        else
        {
            if (newtemp)
            {
                MXS_FREE(newtemp->name);
                MXS_FREE(newtemp);
            }
            MXS_FREE(templates->name);
            strlink_free(templates->rulenames);
            MXS_FREE(templates);
            return false;
        }
        user = user->next;
    }

    templates->next = rstack->templates;
    rstack->templates = templates;

    strlink_free(rstack->user);
    strlink_free(rstack->active_rules);
    rstack->user = NULL;
    rstack->active_rules = NULL;
    return true;
}

void free_user_templates(user_template_t *templates)
{
    while (templates)
    {
        user_template_t *tmp = templates;
        templates = templates->next;
        strlink_free(tmp->rulenames);
        MXS_FREE(tmp->name);
        MXS_FREE(tmp);
    }
}

void set_matching_mode(void* scanner, enum match_type mode)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->active_mode = mode;
}

/**
 * Define the topmost rule as a wildcard rule
 * @param scanner Current scanner
 */
void define_wildcard_rule(void* scanner)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->rule->type = RT_WILDCARD;
}

/**
 * Remove backticks from a string
 * @param string String to parse
 * @return String without backticks
 */
static char* strip_backticks(char* string)
{
    char* ptr = strchr(string, '`');
    if (ptr)
    {
        char *end = strrchr(string, '`');
        ss_dassert(end);
        *end = '\0';
        return ptr + 1;
    }
    return string;
}

/**
 * Define the current rule as a columns rule
 * @param scanner Current scanner
 * @param columns List of column names
 */
bool define_columns_rule(void* scanner, char* columns)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    STRLINK* list = NULL;

    if ((list = strlink_push(rstack->rule->data, strip_backticks(columns))))
    {
        rstack->rule->type = RT_COLUMN;
        rstack->rule->data = list;
    }

    return list != NULL;
}

/**
 * Define the current rule as a function rule
 * @param scanner Current scanner
 * @param columns List of function names
 */
bool define_function_rule(void* scanner, char* columns)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    STRLINK* list = NULL;

    if ((list = strlink_push(rstack->rule->data, strip_backticks(columns))))
    {
        rstack->rule->type = RT_FUNCTION;
        rstack->rule->data = list;
    }

    return list != NULL;
}

/**
 * Define the topmost rule as a no_where_clause rule
 * @param scanner Current scanner
 */
void define_where_clause_rule(void* scanner)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->rule->type = RT_CLAUSE;
}

/**
 * Define the topmost rule as a no_where_clause rule
 * @param scanner Current scanner
 */
bool define_limit_queries_rule(void* scanner, int max, int timeperiod, int holdoff)
{
    struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    QUERYSPEED* qs = MXS_MALLOC(sizeof(QUERYSPEED));

    if (qs)
    {
        qs->limit = max;
        qs->period = timeperiod;
        qs->cooldown = holdoff;
        rstack->rule->type = RT_THROTTLE;
        rstack->rule->data = qs;
    }

    return qs != NULL;
}

/**
 * Define the topmost rule as a regex rule
 * @param scanner Current scanner
 * @param pattern Quoted regex pattern
 */
bool define_regex_rule(void* scanner, char* pattern)
{
    /** This should never fail as long as the rule syntax is correct */
    PCRE2_SPTR start = (PCRE2_SPTR) get_regex_string(&pattern);
    ss_dassert(start);
    pcre2_code *re;
    int err;
    size_t offset;
    if ((re = pcre2_compile(start, PCRE2_ZERO_TERMINATED,
                            0, &err, &offset, NULL)))
    {
        struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
        ss_dassert(rstack);
        rstack->rule->type = RT_REGEX;
        rstack->rule->data = (void*) re;
    }
    else
    {
        PCRE2_UCHAR errbuf[MXS_STRERROR_BUFLEN];
        pcre2_get_error_message(err, errbuf, sizeof(errbuf));
        MXS_ERROR("Invalid regular expression '%s': %s",
                  start, errbuf);
    }

    return re != NULL;
}

/**
 * @brief Process the user templates into actual user definitions
 *
 * @param instance Filter instance
 * @param templates User templates
 * @param rules List of all rules
 * @return True on success, false on error.
 */
static bool process_user_templates(HASHTABLE *users, user_template_t *templates,
                                   RULE* rules)
{
    bool rval = true;

    if (templates == NULL)
    {
        MXS_ERROR("No user definitions found in the rule file.");
        rval = false;
    }

    while (templates)
    {
        DBFW_USER *user = hashtable_fetch(users, templates->name);

        if (user == NULL)
        {
            if ((user = MXS_MALLOC(sizeof(DBFW_USER))) && (user->name = MXS_STRDUP(templates->name)))
            {
                user->rules_and = NULL;
                user->rules_or = NULL;
                user->rules_strict_and = NULL;
                user->qs_limit = NULL;
                spinlock_init(&user->lock);
                hashtable_add(users, user->name, user);
            }
            else
            {
                MXS_FREE(user);
                rval = false;
                break;
            }
        }

        RULE_BOOK *foundrules = NULL;
        RULE *rule;
        STRLINK *names = templates->rulenames;

        while (names && (rule = find_rule_by_name(rules, names->value)))
        {
            foundrules = rulebook_push(foundrules, rule);
            names = names->next;
        }

        if (foundrules)
        {
            RULE_BOOK *tail = foundrules;

            while (tail->next)
            {
                tail = tail->next;
            }

            switch (templates->type)
            {
            case FWTOK_MATCH_ANY:
                tail->next = user->rules_or;
                user->rules_or = foundrules;
                break;

            case FWTOK_MATCH_ALL:
                tail->next = user->rules_and;
                user->rules_and = foundrules;
                break;

            case FWTOK_MATCH_STRICT_ALL:
                tail->next = user->rules_strict_and;
                user->rules_strict_and = foundrules;
                break;
            }
        }
        else
        {
            MXS_ERROR("Could not find definition for rule '%s'.", names->value);
            rval = false;
            break;
        }
        templates = templates->next;
    }

    return rval;
}

/**
 * Read a rule file from disk and process it into rule and user definitions
 * @param filename Name of the file
 * @param instance Filter instance
 * @return True on success, false on error.
 */
static bool process_rule_file(const char* filename, RULE** rules, HASHTABLE **users)
{
    int rc = 1;
    FILE *file = fopen(filename, "r");

    if (file)
    {
        yyscan_t scanner;
        struct parser_stack pstack;

        pstack.rule = NULL;
        pstack.user = NULL;
        pstack.active_rules = NULL;
        pstack.templates = NULL;

        dbfw_yylex_init(&scanner);
        YY_BUFFER_STATE buf = dbfw_yy_create_buffer(file, YY_BUF_SIZE, scanner);
        dbfw_yyset_extra(&pstack, scanner);
        dbfw_yy_switch_to_buffer(buf, scanner);

        /** Parse the rule file */
        rc = dbfw_yyparse(scanner);

        dbfw_yy_delete_buffer(buf, scanner);
        dbfw_yylex_destroy(scanner);
        fclose(file);
        HASHTABLE *new_users = dbfw_userlist_create();

        if (rc == 0 && new_users && process_user_templates(new_users, pstack.templates, pstack.rule))
        {
            *rules = pstack.rule;
            *users = new_users;
        }
        else
        {
            rc = 1;
            rule_free_all(pstack.rule);
            hashtable_free(new_users);
            MXS_ERROR("Failed to process rule file '%s'.", filename);
        }

        free_user_templates(pstack.templates);
        strlink_free(pstack.active_rules);
        strlink_free(pstack.user);
    }
    else
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to open rule file '%s': %d, %s", filename, errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));

    }

    return rc == 0;
}

/**
 * @brief Replace the rule file used by this thread
 *
 * This function replaces or initializes the thread local list of rules and users.
 *
 * @param instance Filter instance
 * @return True if the session can continue, false on fatal error.
 */
bool replace_rules(FW_INSTANCE* instance)
{
    bool rval = true;
    spinlock_acquire(&instance->lock);

    size_t len = strlen(instance->rulefile);
    char filename[len + 1];
    strcpy(filename, instance->rulefile);

    spinlock_release(&instance->lock);

    RULE *rules;
    HASHTABLE *users;

    if (process_rule_file(filename, &rules, &users))
    {
        rule_free_all(thr_rules);
        hashtable_free(thr_users);
        thr_rules = rules;
        thr_users = users;
        rval = true;
    }
    else if (thr_rules && thr_users)
    {
        MXS_ERROR("Failed to parse rules at '%s'. Old rules are still used.", filename);
    }
    else
    {
        MXS_ERROR("Failed to parse rules at '%s'. No previous rules available, "
                  "closing session.", filename);
        rval = false;
    }

    return rval;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name     The name of the instance (as defined in the config file).
 * @param options  The options for this filter
 * @param params   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    FW_INSTANCE *my_instance = MXS_CALLOC(1, sizeof(FW_INSTANCE));

    if (my_instance == NULL)
    {
        MXS_FREE(my_instance);
        return NULL;
    }

    spinlock_init(&my_instance->lock);
    my_instance->action = config_get_enum(params, "action", action_values);
    my_instance->log_match = FW_LOG_NONE;

    if (config_get_bool(params, "log_match"))
    {
        my_instance->log_match |= FW_LOG_MATCH;
    }

    if (config_get_bool(params, "log_no_match"))
    {
        my_instance->log_match |= FW_LOG_NO_MATCH;
    }

    RULE *rules = NULL;
    HASHTABLE *users = NULL;
    my_instance->rulefile = MXS_STRDUP(config_get_string(params, "rules"));

    if (!my_instance->rulefile || !process_rule_file(my_instance->rulefile, &rules, &users))
    {
        MXS_FREE(my_instance);
        my_instance = NULL;
    }
    else
    {
        atomic_add(&my_instance->rule_version, 1);
    }

    rule_free_all(rules);
    hashtable_free(users);

    return (MXS_FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *
newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    FW_SESSION *my_session;

    if ((my_session = MXS_CALLOC(1, sizeof(FW_SESSION))) == NULL)
    {
        return NULL;
    }
    my_session->session = session;
    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
}

/**
 * Free the memory associated with the session
 *
 * @param instance  The filter instance
 * @param session   The filter session
 */
static void
freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    FW_SESSION *my_session = (FW_SESSION *) session;
    MXS_FREE(my_session->errmsg);
    MXS_FREE(my_session->query_speed);
    MXS_FREE(my_session);
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param downstream    The downstream filter or router.
 */
static void
setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_DOWNSTREAM *downstream)
{
    FW_SESSION *my_session = (FW_SESSION *) session;
    my_session->down = *downstream;
}

/**
 * Generates a dummy error packet for the client with a custom message.
 * @param session The FW_SESSION object
 * @param msg Custom error message for the packet.
 * @return The dummy packet or NULL if an error occurred
 */
GWBUF* gen_dummy_error(FW_SESSION* session, char* msg)
{
    GWBUF* buf;
    char* errmsg;
    DCB* dcb;
    MYSQL_session* mysql_session;
    unsigned int errlen;

    if (session == NULL || session->session == NULL ||
        session->session->client_dcb == NULL ||
        session->session->client_dcb->data == NULL)
    {
        MXS_ERROR("Firewall filter session missing data.");
        return NULL;
    }

    dcb = session->session->client_dcb;
    mysql_session = (MYSQL_session*) dcb->data;
    errlen = msg != NULL ? strlen(msg) : 0;
    errmsg = (char*) MXS_MALLOC((512 + errlen) * sizeof(char));

    if (errmsg == NULL)
    {
        return NULL;
    }


    if (mysql_session->db[0] == '\0')
    {
        sprintf(errmsg, "Access denied for user '%s'@'%s'", dcb->user, dcb->remote);
    }
    else
    {
        sprintf(errmsg, "Access denied for user '%s'@'%s' to database '%s'",
                dcb->user, dcb->remote, mysql_session->db);
    }

    if (msg != NULL)
    {
        char* ptr = strchr(errmsg, '\0');
        sprintf(ptr, ": %s", msg);

    }

    buf = modutil_create_mysql_err_msg(1, 0, 1141, "HY000", (const char*) errmsg);
    MXS_FREE(errmsg);

    return buf;
}

/**
 * Checks if the timerange object is active.
 * @return Whether the timerange is active
 */
bool inside_timerange(TIMERANGE* comp)
{

    struct tm tm_now;
    struct tm tm_before, tm_after;
    time_t before, after, now, time_now;
    double to_before, to_after;

    time(&time_now);
    localtime_r(&time_now, &tm_now);
    memcpy(&tm_before, &tm_now, sizeof(struct tm));
    memcpy(&tm_after, &tm_now, sizeof(struct tm));


    tm_before.tm_sec = comp->start.tm_sec;
    tm_before.tm_min = comp->start.tm_min;
    tm_before.tm_hour = comp->start.tm_hour;
    tm_after.tm_sec = comp->end.tm_sec;
    tm_after.tm_min = comp->end.tm_min;
    tm_after.tm_hour = comp->end.tm_hour;


    before = mktime(&tm_before);
    after = mktime(&tm_after);
    now = mktime(&tm_now);
    to_before = difftime(now, before);
    to_after = difftime(now, after);

    if (to_before > 0.0 && to_after < 0.0)
    {
        return true;
    }
    return false;
}

/**
 * Checks for active timeranges for a given rule.
 * @param rule Pointer to a RULE object
 * @return true if the rule is active
 */
bool rule_is_active(RULE* rule)
{
    TIMERANGE* times;
    if (rule->active != NULL)
    {
        times = (TIMERANGE*) rule->active;
        while (times)
        {
            if (inside_timerange(times))
            {
                return true;
            }
            times = times->next;
        }
        return false;
    }
    return true;
}

/**
 * Log and create an error message when a query could not be fully parsed.
 * @param my_instance The FwFilter instance.
 * @param reason The reason the query was rejected.
 * @param query The query that could not be parsed.
 * @param matchesp Pointer to variable that will receive the value indicating
 *                 whether the query was parsed or not.
 *
 * Note that the value of *matchesp depends on the the mode of the filter,
 * i.e., whether it is in whitelist or blacklist mode. The point is that
 * irrespective of the mode, the query must be rejected.
 */
static char* create_parse_error(FW_INSTANCE* my_instance,
                                const char* reason,
                                const char* query,
                                bool* matchesp)
{
    char *msg = NULL;

    char format[] =
        "Query could not be %s and will hence be rejected. "
        "Please ensure that the SQL syntax is correct";
    size_t len = sizeof(format) + strlen(reason); // sizeof includes the trailing NULL as well.
    char message[len];
    sprintf(message, format, reason);
    MXS_WARNING("%s: %s", message, query);

    if ((my_instance->action == FW_ACTION_ALLOW) || (my_instance->action == FW_ACTION_BLOCK))
    {
        char msgbuf[len + 1]; // +1 for the "."
        sprintf(msgbuf, "%s.", message);
        msg = MXS_STRDUP_A(msgbuf);

        if (my_instance->action == FW_ACTION_ALLOW)
        {
            *matchesp = false;
        }
        else
        {
            *matchesp = true;
        }
    }

    return msg;
}

bool match_throttle(FW_SESSION* my_session, RULE_BOOK *rulebook, char **msg)
{
    bool matches = false;
    QUERYSPEED* rule_qs = (QUERYSPEED*)rulebook->rule->data;
    QUERYSPEED* queryspeed = my_session->query_speed;
    time_t time_now = time(NULL);
    char emsg[512];

    if (queryspeed == NULL)
    {
        /**No match found*/
        queryspeed = (QUERYSPEED*)MXS_CALLOC(1, sizeof(QUERYSPEED));
        MXS_ABORT_IF_NULL(queryspeed);
        queryspeed->period = rule_qs->period;
        queryspeed->cooldown = rule_qs->cooldown;
        queryspeed->limit = rule_qs->limit;
        my_session->query_speed = queryspeed;
    }

    if (queryspeed->active)
    {
        if (difftime(time_now, queryspeed->triggered) < queryspeed->cooldown)
        {
            double blocked_for = queryspeed->cooldown - difftime(time_now, queryspeed->triggered);
            sprintf(emsg, "Queries denied for %f seconds", blocked_for);
            *msg = MXS_STRDUP_A(emsg);
            matches = true;

            MXS_INFO("rule '%s': user denied for %f seconds",
                     rulebook->rule->name, blocked_for);
        }
        else
        {
            queryspeed->active = false;
            queryspeed->count = 0;
        }
    }
    else
    {
        if (queryspeed->count >= queryspeed->limit)
        {
            MXS_INFO("rule '%s': query limit triggered (%d queries in %d seconds), "
                     "denying queries from user for %d seconds.", rulebook->rule->name,
                     queryspeed->limit, queryspeed->period, queryspeed->cooldown);

            queryspeed->triggered = time_now;
            queryspeed->active = true;
            matches = true;

            double blocked_for = queryspeed->cooldown - difftime(time_now, queryspeed->triggered);
            sprintf(emsg, "Queries denied for %f seconds", blocked_for);
            *msg = MXS_STRDUP_A(emsg);
        }
        else if (queryspeed->count > 0 &&
                 difftime(time_now, queryspeed->first_query) <= queryspeed->period)
        {
            queryspeed->count++;
        }
        else
        {
            queryspeed->first_query = time_now;
            queryspeed->count = 1;
        }
    }

    return matches;
}

void match_regex(RULE_BOOK *rulebook, const char *query, bool *matches, char **msg)
{

    pcre2_match_data *mdata = pcre2_match_data_create_from_pattern(rulebook->rule->data, NULL);

    if (mdata)
    {
        if (pcre2_match((pcre2_code*)rulebook->rule->data,
                        (PCRE2_SPTR)query, PCRE2_ZERO_TERMINATED,
                        0, 0, mdata, NULL) > 0)
        {
            MXS_NOTICE("rule '%s': regex matched on query", rulebook->rule->name);
            *matches = true;
            *msg = MXS_STRDUP_A("Permission denied, query matched regular expression.");
        }

        pcre2_match_data_free(mdata);
    }
    else
    {
        MXS_ERROR("Allocation of matching data for PCRE2 failed."
                  " This is most likely caused by a lack of memory");
    }
}

void match_column(RULE_BOOK *rulebook, GWBUF *queue, bool *matches, char **msg)
{
    const QC_FIELD_INFO* infos;
    size_t n_infos;
    qc_get_field_info(queue, &infos, &n_infos);

    for (size_t i = 0; i < n_infos; ++i)
    {
        const char* tok = infos[i].column;

        STRLINK* strln = (STRLINK*)rulebook->rule->data;
        while (strln)
        {
            if (strcasecmp(tok, strln->value) == 0)
            {
                char emsg[strlen(strln->value) + 100];
                sprintf(emsg, "Permission denied to column '%s'.", strln->value);
                MXS_NOTICE("rule '%s': query targets forbidden column: %s",
                           rulebook->rule->name, strln->value);
                *msg = MXS_STRDUP_A(emsg);
                *matches = true;
                break;
            }
            strln = strln->next;
        }
    }
}

void match_function(RULE_BOOK *rulebook, GWBUF *queue, bool *matches, char **msg)
{
    const QC_FUNCTION_INFO* infos;
    size_t n_infos;
    qc_get_function_info(queue, &infos, &n_infos);

    for (size_t i = 0; i < n_infos; ++i)
    {
        const char* tok = infos[i].name;

        STRLINK* strln = (STRLINK*)rulebook->rule->data;
        while (strln)
        {
            if (strcasecmp(tok, strln->value) == 0)
            {
                char emsg[strlen(strln->value) + 100];
                sprintf(emsg, "Permission denied to function '%s'.", strln->value);
                MXS_NOTICE("rule '%s': query uses forbidden function: %s",
                           rulebook->rule->name, strln->value);
                *msg = MXS_STRDUP_A(emsg);
                *matches = true;
                break;
            }
            strln = strln->next;
        }
    }
}

void match_wildcard(RULE_BOOK *rulebook, GWBUF *queue, bool *matches, char **msg)
{
    const QC_FIELD_INFO* infos;
    size_t n_infos;
    qc_get_field_info(queue, &infos, &n_infos);

    for (size_t i = 0; i < n_infos; ++i)
    {
        if (strcmp(infos[i].column, "*") == 0)
        {
            MXS_NOTICE("rule '%s': query contains a wildcard.", rulebook->rule->name);
            *matches = true;
            *msg = MXS_STRDUP_A("Usage of wildcard denied.");
        }
    }
}

/**
 * Check if a query matches a single rule
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param rulebook The rule to check
 * @param query Pointer to the null-terminated query string
 * @return true if the query matches the rule
 */
bool rule_matches(FW_INSTANCE* my_instance,
                  FW_SESSION* my_session,
                  GWBUF *queue,
                  DBFW_USER* user,
                  RULE_BOOK *rulebook,
                  char* query)
{
    char *msg = NULL;
    qc_query_op_t optype = QUERY_OP_UNDEFINED;
    bool matches = false;
    bool is_sql = modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue);

    if (is_sql)
    {
        qc_parse_result_t parse_result = qc_parse(queue, QC_COLLECT_ALL);

        if (parse_result == QC_QUERY_INVALID)
        {
            msg = create_parse_error(my_instance, "tokenized", query, &matches);
            goto queryresolved;
        }
        else
        {
            optype = qc_get_operation(queue);

            if (parse_result != QC_QUERY_PARSED)
            {
                if ((rulebook->rule->type == RT_COLUMN) ||
                    (rulebook->rule->type == RT_FUNCTION) ||
                    (rulebook->rule->type == RT_WILDCARD) ||
                    (rulebook->rule->type == RT_CLAUSE))
                {
                    switch (optype)
                    {
                    case QUERY_OP_SELECT:
                    case QUERY_OP_UPDATE:
                    case QUERY_OP_INSERT:
                    case QUERY_OP_DELETE:
                        // In these cases, we have to be able to trust what qc_get_field_info
                        // returns. Unless the query was parsed completely, we cannot do that.
                        msg = create_parse_error(my_instance, "parsed completely", query, &matches);
                        goto queryresolved;

                    default:
                        break;
                    }
                }
            }
        }
    }

    if (rulebook->rule->on_queries == QUERY_OP_UNDEFINED ||
        rulebook->rule->on_queries & optype ||
        (MYSQL_IS_COM_INIT_DB((uint8_t*)GWBUF_DATA(queue)) &&
         rulebook->rule->on_queries & QUERY_OP_CHANGE_DB))
    {
        switch (rulebook->rule->type)
        {
        case RT_UNDEFINED:
            ss_dassert(false);
            MXS_ERROR("Undefined rule type found.");
            break;

        case RT_REGEX:
            match_regex(rulebook, query, &matches, &msg);
            break;

        case RT_PERMISSION:
            matches = true;
            msg = MXS_STRDUP_A("Permission denied at this time.");
            MXS_NOTICE("rule '%s': query denied at this time.", rulebook->rule->name);
            break;

        case RT_COLUMN:
            if (is_sql)
            {
                match_column(rulebook, queue, &matches, &msg);
            }
            break;

        case RT_FUNCTION:
            if (is_sql)
            {
                match_function(rulebook, queue, &matches, &msg);
            }
            break;

        case RT_WILDCARD:
            if (is_sql)
            {
                match_wildcard(rulebook, queue, &matches, &msg);
            }
            break;

        case RT_THROTTLE:
            matches = match_throttle(my_session, rulebook, &msg);
            break;

        case RT_CLAUSE:
            if (is_sql && !qc_query_has_clause(queue))
            {
                matches = true;
                msg = MXS_STRDUP_A("Required WHERE/HAVING clause is missing.");
                MXS_NOTICE("rule '%s': query has no where/having "
                           "clause, query is denied.", rulebook->rule->name);
            }
            break;

        default:
            break;

        }
    }

queryresolved:
    if (msg)
    {
        if (my_session->errmsg)
        {
            MXS_FREE(my_session->errmsg);
        }

        my_session->errmsg = msg;
    }

    if (matches)
    {
        rulebook->rule->times_matched++;
    }

    return matches;
}

/**
 * Check if the query matches any of the rules in the user's rulebook.
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param user The user whose rulebook is checked
 * @return True if the query matches at least one of the rules otherwise false
 */
bool check_match_any(FW_INSTANCE* my_instance, FW_SESSION* my_session,
                     GWBUF *queue, DBFW_USER* user, char** rulename)
{
    RULE_BOOK* rulebook;
    bool rval = false;

    if ((rulebook = user->rules_or) &&
        (modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue) ||
         MYSQL_IS_COM_INIT_DB((uint8_t*)GWBUF_DATA(queue))))
    {
        char *fullquery = modutil_get_SQL(queue);

        if (fullquery)
        {
            while (rulebook)
            {
                if (!rule_is_active(rulebook->rule))
                {
                    rulebook = rulebook->next;
                    continue;
                }
                if (rule_matches(my_instance, my_session, queue, user, rulebook, fullquery))
                {
                    *rulename = MXS_STRDUP_A(rulebook->rule->name);
                    rval = true;
                    break;
                }
                rulebook = rulebook->next;
            }

            MXS_FREE(fullquery);
        }
    }
    return rval;
}

/**
 * Append and possibly reallocate string
 * @param dest Destination where the string is appended or NULL if nothing has
 * been allocated yet
 * @param size Size of @c dest
 * @param src String to append to @c dest
 */
void append_string(char** dest, size_t* size, const char* src)
{
    if (*dest == NULL)
    {
        *dest = MXS_STRDUP_A(src);
        *size = strlen(src);
    }
    else
    {
        if (*size < strlen(*dest) + strlen(src) + 3)
        {
            size_t newsize = strlen(*dest) + strlen(src) + 3;
            char* tmp = MXS_REALLOC(*dest, newsize);
            if (tmp)
            {
                *size = newsize;
                *dest = tmp;
            }
            else
            {
                return;
            }
        }
        strcat(*dest, ", ");
        strcat(*dest, src);
    }
}

/**
 * Check if the query matches all rules in the user's rulebook.
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param user The user whose rulebook is checked
 * @return True if the query matches all of the rules otherwise false
 */
bool check_match_all(FW_INSTANCE* my_instance, FW_SESSION* my_session,
                     GWBUF *queue, DBFW_USER* user, bool strict_all, char** rulename)
{
    bool rval = false;
    bool have_active_rule = false;
    RULE_BOOK* rulebook = strict_all ? user->rules_strict_and : user->rules_and;
    char *matched_rules = NULL;
    size_t size = 0;

    if (rulebook && (modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue)))
    {
        char *fullquery = modutil_get_SQL(queue);

        if (fullquery)
        {
            rval = true;
            while (rulebook)
            {
                if (!rule_is_active(rulebook->rule))
                {
                    rulebook = rulebook->next;
                    continue;
                }

                have_active_rule = true;

                if (rule_matches(my_instance, my_session, queue, user, rulebook, fullquery))
                {
                    append_string(&matched_rules, &size, rulebook->rule->name);
                }
                else
                {
                    rval = false;
                    if (strict_all)
                    {
                        break;
                    }
                }

                rulebook = rulebook->next;
            }

            if (!have_active_rule)
            {
                /** No active rules */
                rval = false;
            }
            MXS_FREE(fullquery);
        }
    }

    /** Set the list of matched rule names */
    *rulename = matched_rules;

    return rval;
}

/**
 * Retrieve the user specific data for this session
 *
 * @param hash Hashtable containing the user data
 * @param name Username
 * @param remote Remove network address
 * @return The user data or NULL if it was not found
 */
DBFW_USER* find_user_data(HASHTABLE *hash, const char *name, const char *remote)
{
    char nameaddr[strlen(name) + strlen(remote) + 2];
    snprintf(nameaddr, sizeof(nameaddr), "%s@%s", name, remote);
    DBFW_USER* user = (DBFW_USER*) hashtable_fetch(hash, nameaddr);
    if (user == NULL)
    {
        char *ip_start = strchr(nameaddr, '@') + 1;
        while (user == NULL && next_ip_class(ip_start))
        {
            user = (DBFW_USER*) hashtable_fetch(hash, nameaddr);
        }

        if (user == NULL)
        {
            snprintf(nameaddr, sizeof(nameaddr), "%%@%s", remote);
            ip_start = strchr(nameaddr, '@') + 1;
            while (user == NULL && next_ip_class(ip_start))
            {
                user = (DBFW_USER*) hashtable_fetch(hash, nameaddr);
            }
        }
    }
    return user;
}

static bool command_is_mandatory(const GWBUF *buffer)
{
    switch (MYSQL_GET_COMMAND((uint8_t*)GWBUF_DATA(buffer)))
    {
    case MYSQL_COM_CHANGE_USER:
    case MYSQL_COM_FIELD_LIST:
    case MYSQL_COM_PING:
    case MYSQL_COM_PROCESS_INFO:
    case MYSQL_COM_PROCESS_KILL:
    case MYSQL_COM_QUIT:
    case MYSQL_COM_SET_OPTION:
        return true;

    default:
        return false;
    }
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int
routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    FW_SESSION *my_session = (FW_SESSION *) session;
    FW_INSTANCE *my_instance = (FW_INSTANCE *) instance;
    DCB *dcb = my_session->session->client_dcb;
    int rval = 0;
    ss_dassert(dcb && dcb->session);
    int rule_version = my_instance->rule_version;

    if (thr_rule_version < rule_version)
    {
        if (!replace_rules(my_instance))
        {
            return 0;
        }
        thr_rule_version = rule_version;
    }

    uint32_t type = 0;

    if (modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue))
    {
        type = qc_get_type_mask(queue);
    }

    if (modutil_is_SQL(queue) && modutil_count_statements(queue) > 1)
    {
        GWBUF* err = gen_dummy_error(my_session, "This filter does not support "
                                     "multi-statements.");
        gwbuf_free(queue);
        MXS_FREE(my_session->errmsg);
        my_session->errmsg = NULL;
        rval = dcb->func.write(dcb, err);
    }
    else
    {
        GWBUF* analyzed_queue = queue;

        // QUERY_TYPE_PREPARE_STMT need not be handled separately as the
        // information about statements in COM_STMT_PREPARE packets is
        // accessed exactly like the information of COM_QUERY packets. However,
        // with named prepared statements in COM_QUERY packets, we need to take
        // out the preparable statement and base our decisions on that.

        if (qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT))
        {
            analyzed_queue = qc_get_preparable_stmt(queue);
            ss_dassert(analyzed_queue);
        }

        DBFW_USER *user = find_user_data(thr_users, dcb->user, dcb->remote);
        bool query_ok = command_is_mandatory(queue);

        if (user)
        {
            bool match = false;
            char* rname = NULL;

            if (check_match_any(my_instance, my_session, analyzed_queue, user, &rname) ||
                check_match_all(my_instance, my_session, analyzed_queue, user, false, &rname) ||
                check_match_all(my_instance, my_session, analyzed_queue, user, true, &rname))
            {
                match = true;
            }

            switch (my_instance->action)
            {
            case FW_ACTION_ALLOW:
                if (match)
                {
                    query_ok = true;
                }
                break;

            case FW_ACTION_BLOCK:
                if (!match)
                {
                    query_ok = true;
                }
                break;

            case FW_ACTION_IGNORE:
                query_ok = true;
                break;

            default:
                MXS_ERROR("Unknown dbfwfilter action: %d", my_instance->action);
                ss_dassert(false);
                break;
            }

            if (my_instance->log_match != FW_LOG_NONE)
            {
                char *sql;
                int len;
                if (modutil_extract_SQL(analyzed_queue, &sql, &len))
                {
                    len = MXS_MIN(len, FW_MAX_SQL_LEN);
                    if (match && my_instance->log_match & FW_LOG_MATCH)
                    {
                        ss_dassert(rname);
                        MXS_NOTICE("[%s] Rule '%s' for '%s' matched by %s@%s: %.*s",
                                   dcb->service->name, rname, user->name,
                                   dcb->user, dcb->remote, len, sql);
                    }
                    else if (!match && my_instance->log_match & FW_LOG_NO_MATCH)
                    {
                        MXS_NOTICE("[%s] Query for '%s' by %s@%s was not matched: %.*s",
                                   dcb->service->name, user->name, dcb->user,
                                   dcb->remote, len, sql);
                    }
                }
            }

            MXS_FREE(rname);
        }
        /** If the instance is in whitelist mode, only users that have a rule
         * defined for them are allowed */
        else if (my_instance->action != FW_ACTION_ALLOW)
        {
            query_ok = true;
        }

        if (query_ok)
        {
            rval = my_session->down.routeQuery(my_session->down.instance,
                                               my_session->down.session, queue);
        }
        else
        {
            GWBUF* forward = gen_dummy_error(my_session, my_session->errmsg);
            gwbuf_free(queue);
            MXS_FREE(my_session->errmsg);
            my_session->errmsg = NULL;
            rval = dcb->func.write(dcb, forward);
        }
    }

    return rval;
}

/**
 * Diagnostics routine
 *
 * Prints the connection details and the names of the exchange,
 * queue and the routing key.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static void
diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
{
    FW_INSTANCE *my_instance = (FW_INSTANCE *) instance;

    dcb_printf(dcb, "Firewall Filter\n");
    dcb_printf(dcb, "Rule, Type, Times Matched\n");

    for (RULE *rule = thr_rules; rule; rule = rule->next)
    {
        char buf[strlen(rule->name) + 200];
        print_rule(rule, buf);
        dcb_printf(dcb, "%s\n", buf);
    }
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_STMT_INPUT;
}
