/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file dbfwfilter.c
 * @author Markus Mäkelä
 * @date 13.2.2015
 * @version 1.0.0
 * @copyright GPLv2
 * @section secDesc Firewall Filter
 *
 * A filter that acts as a firewall, denying queries that do not meet a set of rules.
 *
 * Filter configuration parameters:
 *@code{.unparsed}
 *		rules=<path to file>			Location of the rule file
 *@endcode
 * Rules are defined in a separate rule file that lists all the rules and the users to whom the
 * rules are applied.
 * Rules follow a simple syntax that denies the queries that meet the requirements of the rules.
 * For example, to define a rule denying users from accessing the column 'salary' between
 * the times 15:00 and 17:00, the following rule is to be configured into the configuration file:
 *@code{.unparsed}
 *		rule block_salary deny columns salary at_times 15:00:00-17:00:00
 *@endcode
 * The users are matched by username and network address. Wildcard values can be provided by
 * using the '%' character.
 * For example, to apply this rule to users John, connecting from any address
 * that starts with the octets 198.168.%, and Jane, connecting from the address 192.168.0.1:
 *@code{.unparsed}
 *		users John@192.168.% Jane@192.168.0.1 match any rules block_salary
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

#include <my_config.h>
#include <stdio.h>
#include <filter.h>
#include <string.h>
#include <atomic.h>
#include <modutil.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <mysql_client_server_protocol.h>
#include <spinlock.h>
#include <skygw_types.h>
#include <time.h>
#include <assert.h>
#include <regex.h>
#include <maxscale_pcre2.h>
#include <dbfwfilter.h>
#include <ruleparser.yy.h>
#include <lex.yy.h>

/** Older versions of Bison don't include the parsing function in the header */
#ifndef dbfw_yyparse
int dbfw_yyparse(void*);
#endif

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_GA,
    FILTER_VERSION,
    "Firewall Filter"
};

static char *version_str = "V1.1.0";

/*
 * The filter entry points
 */
static FILTER *createInstance(char **options, FILTER_PARAMETER **);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);

static FILTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    NULL,
    routeQuery,
    NULL,
    diagnostic,
};

/**
 * Rule types
 */
typedef enum
{
    RT_UNDEFINED = 0x00, /*< Undefined rule */
    RT_COLUMN, /*<  Column name rule*/
    RT_THROTTLE, /*< Query speed rule */
    RT_PERMISSION, /*< Simple denying rule */
    RT_WILDCARD, /*< Wildcard denial rule */
    RT_REGEX, /*< Regex matching rule */
    RT_CLAUSE /*< WHERE-clause requirement rule */
} ruletype_t;

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

/**
 * Linked list of strings.
 */
typedef struct strlink_t
{
    struct strlink_t *next; /*< Next node in the list */
    char* value; /*< Value of the current node */
} STRLINK;

/**
 * A structure defining a range of time
 */
typedef struct timerange_t
{
    struct timerange_t* next; /*< Next node in the list */
    struct tm start; /*< Start of the time range */
    struct tm end; /*< End of the time range */
} TIMERANGE;

/**
 * Query speed measurement and limitation structure
 */
typedef struct queryspeed_t
{
    time_t first_query; /*< Time when the first query occurred */
    time_t triggered; /*< Time when the limit was exceeded */
    int period; /*< Measurement interval in seconds */
    int cooldown; /*< Time the user is denied access for */
    int count; /*< Number of queries done */
    int limit; /*< Maximum number of queries */
    long id; /*< Unique id of the rule */
    bool active; /*< If the rule has been triggered */
    struct queryspeed_t* next; /*< Next node in the list */
} QUERYSPEED;

/**
 * A structure used to identify individual rules and to store their contents
 *
 * Each type of rule has different requirements that are expressed as void pointers.
 * This allows to match an arbitrary set of rules against a user.
 */
typedef struct rule_t
{
    void* data; /*< Actual implementation of the rule */
    char* name; /*< Name of the rule */
    ruletype_t type; /*< Type of the rule */
    qc_query_op_t on_queries; /*< Types of queries to inspect */
    bool allow; /*< Allow or deny the query if this rule matches */
    int times_matched; /*< Number of times this rule has been matched */
    TIMERANGE* active; /*< List of times when this rule is active */
    struct rule_t *next;
} RULE;

/**
 * Linked list of pointers to a global pool of RULE structs
 */
typedef struct rulelist_t
{
    RULE* rule; /*< The rule structure */
    struct rulelist_t* next; /*< Next node in the list */
} RULELIST;

typedef struct user_template
{
    char *name;
    enum match_type type; /** Matching type */
    STRLINK *rulenames; /** names of the rules */
    struct user_template *next;
} user_template_t;

typedef struct user_t
{
    char* name; /*< Name of the user */
    SPINLOCK lock; /*< User spinlock */
    QUERYSPEED* qs_limit; /*< The query speed structure unique to this user */
    RULELIST* rules_or; /*< If any of these rules match the action is triggered */
    RULELIST* rules_and; /*< All of these rules must match for the action to trigger */
    RULELIST* rules_strict_and; /*< rules that skip the rest of the rules if one of them
				 * fails. This is only for rules paired with 'match strict_all'. */

} USER;

/**
 * Linked list of IP adresses and subnet masks
 */
typedef struct iprange_t
{
    struct iprange_t* next; /*< Next node in the list */
    uint32_t ip; /*< IP address */
    uint32_t mask; /*< Network mask */
} IPRANGE;

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
 * The Firewall filter instance.
 */
typedef struct
{
    HASHTABLE* htable; /*< User hashtable */
    RULE* rules; /*< List of all the rules */
    STRLINK* userstrings; /*< Temporary list of raw strings of users */
    enum fw_actions action; /*< Default operation mode, defaults to deny */
    int log_match; /*< Log matching and/or non-matching queries */
    SPINLOCK lock; /*< Instance spinlock */
    int idgen; /*< UID generator */
} FW_INSTANCE;

/**
 * The session structure for Firewall filter.
 */
typedef struct
{
    SESSION* session; /*< Client session structure */
    char* errmsg; /*< Rule specific error message */
    DOWNSTREAM down; /*< Next object in the downstream chain */
    UPSTREAM up; /*< Next object in the upstream chain */
} FW_SESSION;

bool parse_at_times(const char** tok, char** saveptr, RULE* ruledef);
bool parse_limit_queries(FW_INSTANCE* instance, RULE* ruledef, const char* rule, char** saveptr);

/**
 * Push a string onto a string stack
 * @param head Head of the stack
 * @param value value to add
 * @return New top of the stack or NULL if memory allocation fails
 */
static STRLINK* strlink_push(STRLINK* head, const char* value)
{
    STRLINK* link = malloc(sizeof(STRLINK));

    if (link && (link->value = strdup(value)))
    {
        link->next = head;
    }
    else
    {
        free(link);
        link = NULL;
        MXS_ERROR("dbfwfilter: Memory allocation failed.");
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
        free(head->value);
        free(head);
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
        free(tmp->value);
        free(tmp);
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

static RULELIST* rulelist_push(RULELIST *head, RULE *rule)
{
    RULELIST *rval = malloc(sizeof(RULELIST));

    if (rval)
    {
        rval->rule = rule;
        rval->next = head;
    }
    return rval;
}

static void* rulelist_clone(void* fval)
{

    RULELIST *rule = NULL,
        *ptr = (RULELIST*) fval;


    while (ptr)
    {
        RULELIST* tmp = (RULELIST*) malloc(sizeof(RULELIST));
        tmp->next = rule;
        tmp->rule = ptr->rule;
        rule = tmp;
        ptr = ptr->next;
    }

    return(void*) rule;
}

static void* rulelist_free(void* fval)
{
    RULELIST *ptr = (RULELIST*) fval;
    while (ptr)
    {
        RULELIST *tmp = ptr;
        ptr = ptr->next;
        free(tmp);
    }
    return NULL;
}

static void* huserfree(void* fval)
{
    USER* value = (USER*) fval;

    rulelist_free(value->rules_and);
    rulelist_free(value->rules_or);
    rulelist_free(value->rules_strict_and);
    free(value->qs_limit);
    free(value->name);
    free(value);
    return NULL;
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

            tr = (TIMERANGE*) malloc(sizeof(TIMERANGE));

            if (tr)
            {
                tr->start = start;
                tr->end = end;
                tr->next = NULL;
            }
            else
            {
                MXS_ERROR("dbfwfilter: malloc returned NULL.");
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

    tmp = (TIMERANGE*) calloc(1, sizeof(TIMERANGE));
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

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char * version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void ModuleInit()
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
FILTER_OBJECT * GetModuleObject()
{
    return &MyObject;
}

/**
 * Adds the given rule string to the list of strings to be parsed for users.
 * @param rule The rule string, assumed to be null-terminated
 * @param instance The FW_FILTER instance
 */
void add_users(char* rule, FW_INSTANCE* instance)
{
    assert(rule != NULL && instance != NULL);
    instance->userstrings = strlink_push(instance->userstrings, rule);
}

/**
 * Apply a rule set to a user
 *
 * @param instance Filter instance
 * @param user User name
 * @param rulelist List of rules to apply
 * @param type Matching type, one of FWTOK_MATCH_ANY, FWTOK_MATCH_ALL or FWTOK_MATCH_STRICT_ALL
 * @return True of the rules were successfully applied. False if memory allocation
 * fails
 */
static bool apply_rule_to_user(FW_INSTANCE *instance, char *username,
                               RULELIST *rulelist, enum match_type type)
{
    USER* user;
    ss_dassert(type == FWTOK_MATCH_ANY || type == FWTOK_MATCH_STRICT_ALL || type == FWTOK_MATCH_ALL);
    if ((user = (USER*) hashtable_fetch(instance->htable, username)) == NULL)
    {
        /**New user*/
        if ((user = (USER*) calloc(1, sizeof(USER))) == NULL)
        {
            MXS_ERROR("dbfwfilter: failed to allocate memory when parsing rules.");
            return false;
        }
        spinlock_init(&user->lock);
    }

    user->name = (char*) strdup(username);
    user->qs_limit = NULL;
    RULELIST *tl = (RULELIST*) rulelist_clone(rulelist);
    RULELIST *tail = tl;

    while (tail && tail->next)
    {
        tail = tail->next;
    }

    switch (type)
    {
        case FWTOK_MATCH_ANY:
            tail->next = user->rules_or;
            user->rules_or = tl;
            break;
        case FWTOK_MATCH_STRICT_ALL:
            tail->next = user->rules_and;
            user->rules_strict_and = tl;
            break;
        case FWTOK_MATCH_ALL:
            tail->next = user->rules_and;
            user->rules_and = tl;
            break;
    }
    hashtable_add(instance->htable, (void *) username, (void *) user);
    return true;
}

/**
 * Free a TIMERANGE struct
 * @param tr pointer to a TIMERANGE struct
 */
void tr_free(TIMERANGE* tr)
{
    TIMERANGE *node, *tmp;

    node = tr;

    while (node)
    {
        tmp = node;
        node = node->next;
        free(tmp);
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
    char delimiter;
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
    MXS_ERROR("dbfwfilter: Error on line %d, %s: %s\n", dbfw_yyget_lineno(scanner),
              error, dbfw_yyget_text(scanner));
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
    bool rval = true;
    RULE *ruledef = malloc(sizeof(RULE));

    if (ruledef && (ruledef->name = strdup(name)))
    {
        ruledef->type = RT_UNDEFINED;
        ruledef->on_queries = QUERY_OP_UNDEFINED;
        struct parser_stack* rstack = dbfw_yyget_extra((yyscan_t) scanner);
        ss_dassert(rstack);
        ruledef->next = rstack->rule;
        ruledef->active = NULL;
        ruledef->times_matched = 0;
        ruledef->data = NULL;
        rstack->rule = ruledef;
    }
    else
    {
        MXS_ERROR("Memory allocation failed when creating rule '%s'.", name);
        free(ruledef);
        rval = false;
    }

    return rval;
}

/**
 * Free a list of rules
 * @param rule Rules to free
 */
static void free_rules(RULE* rule)
{
    while (rule)
    {
        RULE *tmp = rule->next;
        while (rule->active)
        {
            TIMERANGE *tr = rule->active;
            rule->active = rule->active->next;
            free(tr);
        }

        switch (rule->type)
        {
            case RT_COLUMN:
                strlink_free((STRLINK*) rule->data);
                break;

            case RT_THROTTLE:
                free(rule->data);
                break;

            case RT_REGEX:
                pcre2_code_free((pcre2_code*) rule->data);
                break;

            default:
                break;
        }

        free(rule->name);
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
        user_template_t* newtemp = malloc(sizeof(user_template_t));
        STRLINK* tmp;
        if (newtemp && (newtemp->name = strdup(user->value)) &&
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
                free(newtemp->name);
                free(newtemp);
            }
            MXS_ERROR("Memory allocation failed when processing rule file users definitions.");
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
        free(tmp->name);
        free(tmp);
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
    QUERYSPEED* qs = malloc(sizeof(QUERYSPEED));

    if (qs)
    {
        qs->limit = max;
        qs->period = timeperiod;
        qs->cooldown = holdoff;
        rstack->rule->type = RT_THROTTLE;
        rstack->rule->data = qs;
    }
    else
    {
        MXS_ERROR("dbfwfilter: Memory allocation failed when adding limit_queries rule.");
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
        PCRE2_UCHAR errbuf[STRERROR_BUFLEN];
        pcre2_get_error_message(err, errbuf, sizeof(errbuf));
        MXS_ERROR("dbfwfilter: Invalid regular expression '%s': %s",
                  start, errbuf);
    }

    return re != NULL;
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
 * @brief Process the user templates into actual user definitions
 *
 * @param instance Filter instance
 * @param templates User templates
 * @param rules List of all rules
 * @return True on success, false on error.
 */
static bool process_user_templates(FW_INSTANCE *instance, user_template_t *templates,
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
        USER *user = hashtable_fetch(instance->htable, templates->name);

        if (user == NULL)
        {
            if ((user = malloc(sizeof(USER)))&& (user->name = strdup(templates->name)))
            {
                user->rules_and = NULL;
                user->rules_or = NULL;
                user->rules_strict_and = NULL;
                spinlock_init(&user->lock);
                hashtable_add(instance->htable, user->name, user);
            }
            else
            {
                free(user);
                rval = false;
                break;
                MXS_ERROR("Memory allocation failed when creating user '%s'.",
                          templates->name);
            }
        }

        RULELIST *foundrules = NULL;
        RULE *rule;
        STRLINK *names = templates->rulenames;

        while (names && (rule = find_rule_by_name(rules, names->value)))
        {
            foundrules = rulelist_push(foundrules, rule);
            names = names->next;
        }

        if (foundrules)
        {
            RULELIST *tail = foundrules;

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
static bool process_rule_file(const char* filename, FW_INSTANCE* instance)
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

        if (rc == 0 && process_user_templates(instance, pstack.templates, pstack.rule))
        {
            instance->rules = pstack.rule;
        }
        else
        {
            rc = 1;
            free_rules(pstack.rule);
            MXS_ERROR("Failed to process rule file '%s'.", filename);
        }

        free_user_templates(pstack.templates);
        strlink_free(pstack.active_rules);
        strlink_free(pstack.user);
    }
    else
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to open rule file '%s': %d, %s", filename, errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));

    }

    return rc == 0;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param options	The options for this filter
 *
 * @return The instance data for this new instance
 */
static FILTER *
createInstance(char **options, FILTER_PARAMETER **params)
{
    FW_INSTANCE *my_instance;
    int i;
    HASHTABLE* ht;
    char *filename = NULL;
    bool err = false;

    if ((my_instance = calloc(1, sizeof(FW_INSTANCE))) == NULL)
    {
        free(my_instance);
        MXS_ERROR("Memory allocation for firewall filter failed.");
        return NULL;
    }

    spinlock_init(&my_instance->lock);

    if ((ht = hashtable_alloc(100, simple_str_hash, strcmp)) == NULL)
    {
        MXS_ERROR("Unable to allocate hashtable.");
        free(my_instance);
        return NULL;
    }

    hashtable_memory_fns(ht, (HASHMEMORYFN) strdup, NULL, (HASHMEMORYFN) free, huserfree);

    my_instance->htable = ht;
    my_instance->action = FW_ACTION_BLOCK;
    my_instance->log_match = FW_LOG_NONE;
    my_instance->userstrings = NULL;

    for (i = 0; params[i]; i++)
    {
        if (strcmp(params[i]->name, "rules") == 0)
        {
            filename = params[i]->value;
        }
        else if (strcmp(params[i]->name, "log_match") == 0 &&
                 config_truth_value(params[i]->value))
        {
            my_instance->log_match |= FW_LOG_MATCH;
        }
        else if (strcmp(params[i]->name, "log_no_match") == 0 &&
                 config_truth_value(params[i]->value))
        {
            my_instance->log_match |= FW_LOG_NO_MATCH;
        }
        else if (strcmp(params[i]->name, "action") == 0)
        {
            if (strcmp(params[i]->value, "allow") == 0)
            {
                my_instance->action = FW_ACTION_ALLOW;
            }
            else if (strcmp(params[i]->value, "block") == 0)
            {
                my_instance->action = FW_ACTION_BLOCK;
            }
            else if (strcmp(params[i]->value, "ignore") == 0)
            {
                my_instance->action = FW_ACTION_IGNORE;
            }
            else
            {
                MXS_ERROR("Unknown value for %s: %s. Expected one of 'allow', "
                          "'block' or 'ignore'.", params[i]->name, params[i]->value);
                err = true;
            }
        }
        else if (!filter_standard_parameter(params[i]->name))
        {
            MXS_ERROR("Unknown parameter '%s' for dbfwfilter.", params[i]->name);
            err = true;
        }
    }

    if (filename == NULL)
    {
        MXS_ERROR("Unable to find rule file for firewall filter. Please provide the path with"
                  " rules=<path to file>");
        err = true;
    }

    if (err || !process_rule_file(filename, my_instance))
    {
        hashtable_free(my_instance->htable);
        free(my_instance);
        my_instance = NULL;
    }

    return(FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static void *
newSession(FILTER *instance, SESSION *session)
{
    FW_SESSION *my_session;

    if ((my_session = calloc(1, sizeof(FW_SESSION))) == NULL)
    {
        return NULL;
    }
    my_session->session = session;
    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static void
closeSession(FILTER *instance, void *session)
{
}

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
    FW_SESSION *my_session = (FW_SESSION *) session;
    if (my_session->errmsg)
    {
        free(my_session->errmsg);
    }
    free(my_session);
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param downstream	The downstream filter or router.
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
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
    errmsg = (char*) malloc((512 + errlen) * sizeof(char));

    if (errmsg == NULL)
    {
        MXS_ERROR("Memory allocation failed.");
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
    free(errmsg);

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
 * Check if a query matches a single rule
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param rulelist The rule to check
 * @param query Pointer to the null-terminated query string
 * @return true if the query matches the rule
 */
bool rule_matches(FW_INSTANCE* my_instance,
                  FW_SESSION* my_session,
                  GWBUF *queue,
                  USER* user,
                  RULELIST *rulelist,
                  char* query)
{
    char *ptr, *where, *msg = NULL;
    char emsg[512];

    unsigned char* memptr = (unsigned char*) queue->start;
    bool is_sql, is_real, matches;
    qc_query_op_t optype = QUERY_OP_UNDEFINED;
    STRLINK* strln = NULL;
    QUERYSPEED* queryspeed = NULL;
    QUERYSPEED* rule_qs = NULL;
    time_t time_now;
    struct tm tm_now;

    time(&time_now);
    localtime_r(&time_now, &tm_now);

    matches = false;
    is_sql = modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue);

    if (is_sql)
    {
        optype = qc_get_operation(queue);
        is_real = qc_is_real_query(queue);
    }

    if (rulelist->rule->on_queries == QUERY_OP_UNDEFINED || rulelist->rule->on_queries & optype)
    {
        switch (rulelist->rule->type)
        {
            case RT_UNDEFINED:
                MXS_ERROR("Undefined rule type found.");
                break;

            case RT_REGEX:
                if (query)
                {
                    pcre2_match_data *mdata = pcre2_match_data_create_from_pattern(
                                                                                   rulelist->rule->data, NULL);

                    if (mdata)
                    {
                        if (pcre2_match((pcre2_code*) rulelist->rule->data,
                                        (PCRE2_SPTR) query, PCRE2_ZERO_TERMINATED,
                                        0, 0, mdata, NULL) > 0)
                        {
                            matches = true;
                        }
                        pcre2_match_data_free(mdata);
                        if (matches && !rulelist->rule->allow)
                        {
                            msg = strdup("Permission denied, query matched regular expression.");
                            MXS_INFO("dbfwfilter: rule '%s': regex matched on query", rulelist->rule->name);
                            goto queryresolved;
                        }
                    }
                    else
                    {
                        MXS_ERROR("Allocation of matching data for PCRE2 failed."
                                  " This is most likely caused by a lack of memory");
                    }
                }
                break;

            case RT_PERMISSION:
                if (!rulelist->rule->allow)
                {
                    matches = true;
                    msg = strdup("Permission denied at this time.");
                    char buffer[32]; // asctime documentation requires 26
                    asctime_r(&tm_now, buffer);
                    MXS_INFO("dbfwfilter: rule '%s': query denied at: %s", rulelist->rule->name, buffer);
                    goto queryresolved;
                }
                else
                {
                    break;
                }
                break;

            case RT_COLUMN:
                if (is_sql && is_real)
                {
                    where = qc_get_affected_fields(queue);
                    if (where != NULL)
                    {
                        char* saveptr;
                        char* tok = strtok_r(where, " ", &saveptr);
                        while (tok)
                        {
                            strln = (STRLINK*) rulelist->rule->data;
                            while (strln)
                            {
                                if (strcasecmp(tok, strln->value) == 0)
                                {
                                    matches = true;

                                    if (!rulelist->rule->allow)
                                    {
                                        sprintf(emsg, "Permission denied to column '%s'.", strln->value);
                                        MXS_INFO("dbfwfilter: rule '%s': query targets forbidden column: %s",
                                                 rulelist->rule->name, strln->value);
                                        msg = strdup(emsg);
                                        goto queryresolved;
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }
                                strln = strln->next;
                            }
                            tok = strtok_r(NULL, ",", &saveptr);
                        }
                        free(where);
                    }
                }
                break;

            case RT_WILDCARD:
                if (is_sql && is_real)
                {
                    char * strptr;
                    where = qc_get_affected_fields(queue);

                    if (where != NULL)
                    {
                        strptr = where;

                        if (strchr(strptr, '*'))
                        {

                            matches = true;
                            msg = strdup("Usage of wildcard denied.");
                            MXS_INFO("dbfwfilter: rule '%s': query contains a wildcard.",
                                     rulelist->rule->name);
                            goto queryresolved;
                        }
                        free(where);
                    }
                }
                break;

            case RT_THROTTLE:
                /**
                 * Check if this is the first time this rule is matched and if so, allocate
                 * and initialize a new QUERYSPEED struct for this session.
                 */
                spinlock_acquire(&my_instance->lock);
                rule_qs = (QUERYSPEED*) rulelist->rule->data;
                spinlock_release(&my_instance->lock);

                spinlock_acquire(&user->lock);
                queryspeed = user->qs_limit;
                spinlock_release(&user->lock);

                while (queryspeed)
                {
                    if (queryspeed->id == rule_qs->id)
                    {
                        break;
                    }
                    queryspeed = queryspeed->next;
                }

                if (queryspeed == NULL)
                {

                    /**No match found*/
                    queryspeed = (QUERYSPEED*) calloc(1, sizeof(QUERYSPEED));
                    queryspeed->period = rule_qs->period;
                    queryspeed->cooldown = rule_qs->cooldown;
                    queryspeed->limit = rule_qs->limit;
                    queryspeed->id = rule_qs->id;
                    queryspeed->next = user->qs_limit;
                    user->qs_limit = queryspeed;
                }

                if (queryspeed->active)
                {
                    if (difftime(time_now, queryspeed->triggered) < queryspeed->cooldown)
                    {

                        double blocked_for =
                            queryspeed->cooldown - difftime(time_now, queryspeed->triggered);

                        sprintf(emsg, "Queries denied for %f seconds", blocked_for);
                        MXS_INFO("dbfwfilter: rule '%s': user denied for %f seconds",
                                 rulelist->rule->name, blocked_for);
                        msg = strdup(emsg);
                        matches = true;
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
                        queryspeed->triggered = time_now;
                        matches = true;
                        queryspeed->active = true;

                        MXS_INFO("dbfwfilter: rule '%s': query limit triggered (%d queries in %d seconds), "
                                 "denying queries from user for %d seconds.",
                                 rulelist->rule->name,
                                 queryspeed->limit,
                                 queryspeed->period,
                                 queryspeed->cooldown);
                        double blocked_for =
                            queryspeed->cooldown - difftime(time_now, queryspeed->triggered);
                        sprintf(emsg, "Queries denied for %f seconds", blocked_for);
                        msg = strdup(emsg);
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
                break;

            case RT_CLAUSE:
                if (is_sql && is_real &&
                    !qc_query_has_clause(queue))
                {
                    matches = true;
                    msg = strdup("Required WHERE/HAVING clause is missing.");
                    MXS_INFO("dbfwfilter: rule '%s': query has no where/having "
                             "clause, query is denied.", rulelist->rule->name);
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
            free(my_session->errmsg);
        }

        my_session->errmsg = msg;
    }

    if (matches)
    {
        rulelist->rule->times_matched++;
    }

    return matches;
}

/**
 * Check if the query matches any of the rules in the user's rulelist.
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param user The user whose rulelist is checked
 * @return True if the query matches at least one of the rules otherwise false
 */
bool check_match_any(FW_INSTANCE* my_instance, FW_SESSION* my_session,
                     GWBUF *queue, USER* user, char** rulename)
{
    RULELIST* rulelist;
    bool rval = false;

    if ((rulelist = user->rules_or) &&
        (modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue)))
    {
        char *fullquery = modutil_get_SQL(queue);
        while (rulelist)
        {
            if (!rule_is_active(rulelist->rule))
            {
                rulelist = rulelist->next;
                continue;
            }
            if (rule_matches(my_instance, my_session, queue, user, rulelist, fullquery))
            {
                *rulename = rulelist->rule->name;
                rval = true;
                break;
            }
            rulelist = rulelist->next;
        }

        free(fullquery);
    }
    return rval;
}

/**
 * Check if the query matches all rules in the user's rulelist.
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param user The user whose rulelist is checked
 * @return True if the query matches all of the rules otherwise false
 */
bool check_match_all(FW_INSTANCE* my_instance, FW_SESSION* my_session,
                     GWBUF *queue, USER* user, bool strict_all, char** rulename)
{
    bool rval = false;
    bool have_active_rule = false;
    RULELIST* rulelist = strict_all ? user->rules_strict_and : user->rules_and;

    if (rulelist && (modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue)))
    {
        char *fullquery = modutil_get_SQL(queue);
        rval = true;
        while (rulelist)
        {
            if (!rule_is_active(rulelist->rule))
            {
                rulelist = rulelist->next;
                continue;
            }

            have_active_rule = true;

            if (!rule_matches(my_instance, my_session, queue, user, rulelist, fullquery))
            {
                *rulename = rulelist->rule->name;
                rval = false;
                if (strict_all)
                {
                    break;
                }
            }
            rulelist = rulelist->next;
        }

        if (!have_active_rule)
        {
            /** No active rules */
            rval = false;
        }
        free(fullquery);
    }

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
USER* find_user_data(HASHTABLE *hash, const char *name, const char *remote)
{
    char nameaddr[strlen(name) + strlen(remote) + 2];
    snprintf(nameaddr, sizeof(nameaddr), "%s@%s", name, remote);
    USER* user = (USER*) hashtable_fetch(hash, nameaddr);
    if (user == NULL)
    {
        char *ip_start = strchr(nameaddr, '@') + 1;
        while (user == NULL && next_ip_class(ip_start))
        {
            user = (USER*) hashtable_fetch(hash, nameaddr);
        }

        if (user == NULL)
        {
            snprintf(nameaddr, sizeof(nameaddr), "%%@%s", remote);
            ip_start = strchr(nameaddr, '@') + 1;
            while (user == NULL && next_ip_class(ip_start))
            {
                user = (USER*) hashtable_fetch(hash, nameaddr);
            }
        }
    }
    return user;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    FW_SESSION *my_session = (FW_SESSION *) session;
    FW_INSTANCE *my_instance = (FW_INSTANCE *) instance;
    DCB *dcb = my_session->session->client_dcb;
    int rval = 0;
    ss_dassert(dcb && dcb->session);

    if (modutil_is_SQL(queue) && modutil_count_statements(queue) > 1)
    {
        GWBUF* err = gen_dummy_error(my_session, "This filter does not support "
                                     "multi-statements.");
        gwbuf_free(queue);
        free(my_session->errmsg);
        my_session->errmsg = NULL;
        rval = dcb->func.write(dcb, err);
    }
    else
    {
        USER *user = find_user_data(my_instance->htable, dcb->user, dcb->remote);
        bool query_ok = false;

        if (user)
        {
            bool match = false;
            char* rname;
            if (check_match_any(my_instance, my_session, queue, user, &rname) ||
                check_match_all(my_instance, my_session, queue, user, false, &rname) ||
                check_match_all(my_instance, my_session, queue, user, true, &rname))
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
                if (modutil_extract_SQL(queue, &sql, &len))
                {
                    len = MIN(len, FW_MAX_SQL_LEN);
                    if (match && my_instance->log_match & FW_LOG_MATCH)
                    {
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
            free(my_session->errmsg);
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
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    FW_INSTANCE *my_instance = (FW_INSTANCE *) instance;
    RULE* rules;
    int type;

    if (my_instance)
    {
        spinlock_acquire(&my_instance->lock);
        rules = my_instance->rules;

        dcb_printf(dcb, "Firewall Filter\n");
        dcb_printf(dcb, "%-24s%-24s%-24s\n", "Rule", "Type", "Times Matched");
        while (rules)
        {
            if ((int) rules->type > 0 &&
                (int) rules->type < sizeof(rule_names) / sizeof(char**))
            {
                type = (int) rules->type;
            }
            else
            {
                type = 0;
            }
            dcb_printf(dcb, "%-24s%-24s%-24d\n",
                       rules->name,
                       rule_names[type],
                       rules->times_matched);
            rules = rules->next;
        }
        spinlock_release(&my_instance->lock);
    }
}

#ifdef BUILD_RULE_PARSER
#include <test_utils.h>

int main(int argc, char** argv)
{
    char ch;
    bool have_icase = false;
    char *home;
    char cwd[PATH_MAX];
    char* opts[2] = {NULL, NULL};
    FILTER_PARAMETER ruleparam;
    FILTER_PARAMETER * paramlist[2];

    opterr = 0;
    while ((ch = getopt(argc, argv, "h?")) != -1)
    {
        switch (ch)
        {
            case '?':
            case 'h':
                printf("Usage: %s [OPTION]... RULEFILE\n"
                       "Options:\n"
                       "\t-?\tPrint this information\n",
                       argv[0]);
                return 0;
            default:
                printf("Unknown option '%c'.\n", ch);
                return 1;
        }
    }

    if (argc < 2)
    {
        printf("Usage: %s [OPTION]... RULEFILE\n"
               "-?\tPrint this information\n",
               argv[0]);
        return 1;
    }

    home = malloc(sizeof(char)*(PATH_MAX + 1));
    if (getcwd(home, PATH_MAX) == NULL)
    {
        free(home);
        home = NULL;
    }

    printf("Log files written to: %s\n", home ? home : "/tpm");

    int argc_ = 2;
    char* argv_[] ={
                     "log_manager",
                     "-o",
                     NULL
    };

    mxs_log_init(argc_, argv_);


    init_test_env(home);
    ruleparam.name = strdup("rules");
    ruleparam.value = strdup(argv[1]);
    paramlist[0] = &ruleparam;
    paramlist[1] = NULL;

    if (createInstance(opts, paramlist))
    {
        printf("Rule parsing was successful.\n");
    }
    else
    {
        printf("Failed to parse rule. Read the error log for the reason of the failure.\n");
    }

    mxs_log_flush_sync();

    return 0;
}

#endif
