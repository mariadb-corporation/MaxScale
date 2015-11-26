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
 * Rules are defined in a separate rule file that lists all the rules and the users to whom the rules are applied.
 * Rules follow a simple syntax that denies the queries that meet the requirements of the rules.
 * For example, to define a rule denying users from accessing the column 'salary' between
 * the times 15:00 and 17:00, the following rule is to be configured into the configuration file:
 *@code{.unparsed}
 *		rule block_salary deny columns salary at_times 15:00:00-17:00:00
 *@endcode
 * The users are matched by username and network address. Wildcard values can be provided by using the '%' character.
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
 * rule NAME deny [wildcard | columns VALUE ... | regex REGEX | limit_queries COUNT TIMEPERIOD HOLDOFF | no_where_clause] [at_times VALUE...] [on_queries [select|update|insert|delete]]
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

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_ALPHA_RELEASE,
    FILTER_VERSION,
    "Firewall Filter"
};

static char *version_str = "V1.0.0";

static char* required_rules[] =
{
    "wildcard",
    "columns",
    "regex",
    "limit_queries",
    "no_where_clause",
    NULL
};
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
    double period; /*< Measurement interval in seconds */
    double cooldown; /*< Time the user is denied access for */
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
    skygw_query_op_t on_queries; /*< Types of queries to inspect */
    bool allow; /*< Allow or deny the query if this rule matches */
    int times_matched; /*< Number of times this rule has been matched */
    TIMERANGE* active; /*< List of times when this rule is active */
} RULE;

/**
 * Linked list of pointers to a global pool of RULE structs
 */
typedef struct rulelist_t
{
    RULE* rule; /*< The rule structure */
    struct rulelist_t* next; /*< Next node in the list */
} RULELIST;

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
 * The Firewall filter instance.
 */
typedef struct
{
    HASHTABLE* htable; /*< User hashtable */
    RULELIST* rules; /*< List of all the rules */
    STRLINK* userstrings; /*< Temporary list of raw strings of users */
    bool def_op; /*< Default operation mode, defaults to deny */
    SPINLOCK* lock; /*< Instance spinlock */
    int idgen; /*< UID generator */
    int regflags;
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

static int hashkeyfun(void* key);
static int hashcmpfun(void *, void *);
bool parse_at_times(const char** tok, char** saveptr, RULE* ruledef);
bool parse_limit_queries(FW_INSTANCE* instance, RULE* ruledef, const char* rule, char** saveptr);
/**
 * Hashtable key hashing function. Uses a simple string hashing algorithm.
 * @param key Key to hash
 * @return The hash value of the key
 */
static int hashkeyfun(
                      void* key)
{
    if (key == NULL)
    {
        return 0;
    }
    unsigned int hash = 0, c = 0;
    char* ptr = (char*) key;
    while ((c = *ptr++))
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

/**
 * Hashtable entry comparison function. Does a string matching operation on the
 * two keys. This function assumes the values are pointers to null-terminated
 * character arrays.
 * @param v1 The first key
 * @param v2 The second key
 * @return Zero if the values are equal. Non-zero in other cases.
 */
static int hashcmpfun(
                      void* v1,
                      void* v2)
{
    char* i1 = (char*) v1;
    char* i2 = (char*) v2;

    return strcmp(i1, i2);
}

void* rlistdup(void* fval)
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

static void* hrulefree(void* fval)
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

    hrulefree(value->rules_and);
    hrulefree(value->rules_or);
    hrulefree(value->rules_strict_and);
    free(value->qs_limit);
    free(value->name);
    free(value);
    return NULL;
}

/**
 * Strips the single or double quotes from a string.
 * This function modifies the passed string.
 * @param str String to parse
 * @return Pointer to the modified string
 */
char* strip_tags(char* str)
{

    assert(str != NULL);

    char *ptr = str, *re_start = NULL;
    bool found = false;
    while (*ptr != '\0')
    {

        if (*ptr == '"' ||
            *ptr == '\'')
        {
            if (found)
            {
                *ptr = '\0';
                memmove(str, re_start, ptr - re_start);
                break;
            }
            else
            {
                *ptr = ' ';
                re_start = ptr + 1;
                found = true;
            }
        }
        ptr++;

    }

    return str;
}

/**
 * Parses a string that contains an IP address and converts the last octet to '%'.
 * This modifies the string passed as the parameter.
 * @param str String to parse
 * @return Pointer to modified string or NULL if an error occurred or the string can't be made any less specific
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
 * Parses the strign for the types of queries this rule should be applied to.
 * @param str String to parse
 * @param rule Poiter to a rule
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
 *@return If the timerange is reversed, returns a pointer to the new TIMERANGE otherwise returns a NULL pointer
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
 * Finds the rule with a name matching the passed string.
 *
 * @param tok Name to search for
 * @param instance A valid FW_FILTER instance
 * @return A pointer to the matching RULE if found, else returns NULL
 */
RULE* find_rule(char* tok, FW_INSTANCE* instance)
{
    RULELIST* rlist = instance->rules;

    while (rlist)
    {
        if (strcmp(rlist->rule->name, tok) == 0)
        {
            return rlist->rule;
        }
        rlist = rlist->next;
    }
    MXS_ERROR("Rule not found: %s", tok);
    return NULL;
}

/**
 * Adds the given rule string to the list of strings to be parsed for users.
 * @param rule The rule string, assumed to be null-terminated
 * @param instance The FW_FILTER instance
 */
void add_users(char* rule, FW_INSTANCE* instance)
{
    assert(rule != NULL && instance != NULL);

    STRLINK* link = calloc(1, sizeof(STRLINK));
    if (link == NULL)
    {
        MXS_ERROR("Memory allocation failed");
        return;
    }
    link->next = instance->userstrings;
    link->value = strdup(rule);
    instance->userstrings = link;
}

/**
 * Parses the list of rule strings for users and links them against the listed rules.
 * Only adds those rules that are found. If the rule isn't found a message is written to the error log.
 * @param rule Rule string to parse
 * @param instance The FW_FILTER instance
 */
bool link_rules(char* orig, FW_INSTANCE* instance)
{

    /**Apply rules to users*/

    bool match_any = true;
    bool rval = true;
    char *rule = strdup(orig);
    char *tok, *ruleptr, *userptr, *modeptr;
    char *saveptr = NULL;
    RULELIST* rulelist = NULL;
    bool strict = false;
    userptr = strstr(rule, "users ");
    modeptr = strstr(rule, " match ");
    ruleptr = strstr(rule, " rules ");

    if ((userptr == NULL || ruleptr == NULL || modeptr == NULL) ||
        (userptr > modeptr || userptr > ruleptr || modeptr > ruleptr))
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, right keywords not found in the correct order: %s", orig);
        rval = false;
        goto parse_err;
    }

    *modeptr++ = '\0';
    *ruleptr++ = '\0';

    tok = strtok_r(modeptr, " ", &saveptr);

    if (tok == NULL)
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, right keywords not found in the correct order: %s", orig);
        rval = false;
        goto parse_err;
    }

    if (strcmp(tok, "match") == 0)
    {
        tok = strtok_r(NULL, " ", &saveptr);
        if (tok == NULL)
        {
            MXS_ERROR("dbfwfilter: Rule syntax incorrect, missing keyword after 'match': %s", orig);
            rval = false;
            goto parse_err;
        }
        if (strcmp(tok, "any") == 0)
        {
            match_any = true;
        }
        else if (strcmp(tok, "all") == 0)
        {
            match_any = false;
        }
        else if (strcmp(tok, "strict_all") == 0)
        {
            match_any = false;
            strict = true;
        }
        else
        {
            MXS_ERROR("dbfwfilter: Rule syntax incorrect, 'match' was not followed by correct keyword: %s", orig);
            rval = false;
            goto parse_err;
        }
    }
    else
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, bad token: %s", tok);
        rval = false;
        goto parse_err;
    }

    tok = strtok_r(NULL, " ", &saveptr);

    if (tok != NULL)
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, extra token found after 'match' keyword: %s", orig);
        rval = false;
        goto parse_err;
    }

    tok = strtok_r(ruleptr, " ", &saveptr);

    if (tok == NULL)
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, no rules given: %s", orig);
        rval = false;
        goto parse_err;
    }

    tok = strtok_r(NULL, " ", &saveptr);

    if (tok == NULL)
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, no rules given: %s", orig);
        rval = false;
        goto parse_err;
    }

    while (tok)
    {
        RULE* rule_found = NULL;

        if ((rule_found = find_rule(tok, instance)) != NULL)
        {
            RULELIST* tmp_rl = (RULELIST*) calloc(1, sizeof(RULELIST));
            tmp_rl->rule = rule_found;
            tmp_rl->next = rulelist;
            rulelist = tmp_rl;

        }
        else
        {
            MXS_ERROR("dbfwfilter: Rule syntax incorrect, could not find rule '%s'.", tok);
            rval = false;
            goto parse_err;
        }
        tok = strtok_r(NULL, " ", &saveptr);
    }

    /**
     * Apply this list of rules to all the listed users
     */

    *(ruleptr) = '\0';
    userptr = strtok_r(rule, " ", &saveptr);
    userptr = strtok_r(NULL, " ", &saveptr);

    if (userptr == NULL)
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, no users given: %s", orig);
        rval = false;
        goto parse_err;
    }

    if (rulelist == NULL)
    {
        MXS_ERROR("dbfwfilter: Rule syntax incorrect, no rules found: %s", orig);
        rval = false;
        goto parse_err;
    }

    while (userptr)
    {
        USER* user;
        RULELIST *tl = NULL, *tail = NULL;

        if ((user = (USER*) hashtable_fetch(instance->htable, userptr)) == NULL)
        {

            /**New user*/
            user = (USER*) calloc(1, sizeof(USER));

            if (user == NULL)
            {
                MXS_ERROR("dbfwfilter: failed to allocate memory when parsing rules.");
                rval = false;
                goto parse_err;
            }

            spinlock_init(&user->lock);
        }

        user->name = (char*) strdup(userptr);
        user->qs_limit = NULL;
        tl = (RULELIST*) rlistdup(rulelist);
        tail = tl;
        while (tail && tail->next)
        {
            tail = tail->next;
        }


        if (match_any)
        {
            tail->next = user->rules_or;
            user->rules_or = tl;
        }
        else if (strict)
        {
            tail->next = user->rules_and;
            user->rules_strict_and = tl;
        }
        else
        {
            tail->next = user->rules_and;
            user->rules_and = tl;
        }

        hashtable_add(instance->htable, (void *) userptr, (void *) user);
        userptr = strtok_r(NULL, " ", &saveptr);
    }
parse_err:

    free(rule);

    while (rulelist)
    {
        RULELIST *tmp = rulelist;
        rulelist = rulelist->next;
        free(tmp);
    }
    return rval;
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

bool parse_rule_definition(FW_INSTANCE* instance, RULE* ruledef, char* rule, char** saveptr)
{
    bool rval = true;
    const char *tok = strtok_r(NULL, " ", saveptr);

    if (tok == NULL)
    {
        MXS_ERROR("dbfwfilter: Rule parsing failed, no allow or deny: %s", rule);
        rval = false;
        goto retblock;
    }

    bool deny, allow;

    if ((allow = (strcmp(tok, "allow") == 0)) ||
        (deny = (strcmp(tok, "deny") == 0)))
    {
        bool req_defined = false, at_def = false, oq_def = false;
        bool mode = allow ? true : false;
        ruledef->allow = mode;
        ruledef->type = RT_PERMISSION;
        tok = strtok_r(NULL, " ,", saveptr);


        while (tok)
        {
            for (int i = 0; required_rules[i] != NULL; i++)
            {
                if (strcmp(tok, required_rules[i]) == 0)
                {
                    if (req_defined)
                    {
                        MXS_ERROR("dbfwfilter: Rule parsing failed, Multiple non-optional rules: %s", rule);
                        rval = false;
                        goto retblock;
                    }
                    else
                    {
                        req_defined = true;
                    }
                }
            }

            if (strcmp(tok, "wildcard") == 0)
            {
                ruledef->type = RT_WILDCARD;
            }
            else if (strcmp(tok, "columns") == 0)
            {
                STRLINK *tail = NULL, *current;
                ruledef->type = RT_COLUMN;
                tok = strtok_r(NULL, " ,", saveptr);
                while (tok && strcmp(tok, "at_times") != 0 &&
                       strcmp(tok, "on_queries") != 0)
                {
                    current = malloc(sizeof(STRLINK));
                    current->value = strdup(tok);
                    current->next = tail;
                    tail = current;
                    tok = strtok_r(NULL, " ,", saveptr);
                }

                ruledef->data = (void*) tail;
                continue;

            }
            else if (strcmp(tok, "at_times") == 0)
            {
                if (at_def)
                {
                    MXS_ERROR("dbfwfilter: Rule parsing failed, multiple 'at_times' tokens: %s", rule);
                    rval = false;
                    goto retblock;
                }

                at_def = true;
                tok = strtok_r(NULL, " ,", saveptr);
                TIMERANGE *tr = NULL;

                if (!parse_at_times(&tok, saveptr, ruledef))
                {
                    rval = false;
                    break;
                }

                if (tok && strcmp(tok, "on_queries") == 0)
                {
                    continue;
                }

            }
            else if (strcmp(tok, "regex") == 0)
            {
                bool escaped = false;
                regex_t *re;
                char* start, *str;
                tok = strtok_r(NULL, " ", saveptr);
                char delim = '\'';
                int n_char = 0;

                if (tok == NULL)
                {
                    MXS_ERROR("dbfwfilter: Rule parsing failed, No regex string.");
                    rval = false;
                    goto retblock;
                }

                if (*tok != '\'' && *tok != '\"')
                {
                    MXS_ERROR("dbfwfilter: Rule parsing failed, regex string not quoted.");
                    rval = false;
                    goto retblock;
                }

                while (*tok == '\'' || *tok == '"')
                {
                    delim = *tok;
                    tok++;
                }

                start = (char*)tok;

                while (isspace(*tok) || *tok == delim)
                {
                    tok++;
                }

                while (n_char < 2048)
                {

                    /** Hard-coded regex length cap */

                    if ((*tok == delim) && !escaped)
                    {
                        break;
                    }
                    escaped = (*tok == '\\');
                    tok++;
                    n_char++;
                }

                if (n_char >= 2048)
                {
                    MXS_ERROR("dbfwfilter: Failed to parse rule, regular expression length is over 2048 characters.");
                    rval = false;
                    goto retblock;
                }

                str = calloc(((tok - start) + 1), sizeof(char));
                if (str == NULL)
                {
                    MXS_ERROR("Fatal Error: malloc returned NULL.");
                    rval = false;
                    goto retblock;
                }
                re = (regex_t*) malloc(sizeof(regex_t));

                if (re == NULL)
                {
                    MXS_ERROR("Fatal Error: malloc returned NULL.");
                    rval = false;
                    free(str);
                    goto retblock;
                }

                memcpy(str, start, (tok - start));

                if (regcomp(re, str, REG_NOSUB | instance->regflags))
                {
                    MXS_ERROR("dbfwfilter: Invalid regular expression '%s'.", str);
                    rval = false;
                    free(re);
                    goto retblock;
                }
                else
                {
                    ruledef->type = RT_REGEX;
                    ruledef->data = (void*) re;
                }
                free(str);

            }
            else if (strcmp(tok, "limit_queries") == 0)
            {
                if (!parse_limit_queries(instance, ruledef, rule, saveptr))
                {
                   rval = false;
                   break;
                }
            }
            else if (strcmp(tok, "no_where_clause") == 0)
            {
                ruledef->type = RT_CLAUSE;
                ruledef->data = (void*) mode;
            }
            else if (strcmp(tok, "on_queries") == 0)
            {
                if (oq_def)
                {
                    MXS_ERROR("dbfwfilter: Rule parsing failed, multiple 'on_queries' tokens: %s", rule);
                    rval = false;
                    goto retblock;
                }
                oq_def = true;
                tok = strtok_r(NULL, " ", saveptr);

                if (tok == NULL)
                {
                    MXS_ERROR("dbfwfilter: Missing parameter for 'on_queries'.");
                    rval = false;
                    goto retblock;
                }

                if (!parse_querytypes(tok, ruledef))
                {
                    MXS_ERROR("dbfwfilter: Invalid query type requirements: %s.", tok);
                    rval = false;
                    goto retblock;
                }
            }
            else
            {
                MXS_ERROR("dbfwfilter: Unknown rule type: %s", tok);
                rval = false;
                goto retblock;
            }
            tok = strtok_r(NULL, " ,", saveptr);
        }

        goto retblock;
    }
    retblock:

    return rval;
}

/**
 * Parse the configuration value either as a new rule or a list of users.
 * @param rule The string to parse
 * @param instance The FW_FILTER instance
 */
bool parse_rule(char* rulestr, FW_INSTANCE* instance)
{
    ss_dassert(rulestr != NULL && instance != NULL);

    char rule[strlen(rulestr) + 1];
    strcpy(rule, rulestr);
    char *saveptr = NULL;
    char *tok = strtok_r(rule, " ", &saveptr);
    bool rval = false;

    if (tok)
    {
        if (strcmp("rule", tok) == 0)
        {
            /**Define a new rule*/
            tok = strtok_r(NULL, " ", &saveptr);
            if (tok)
            {
                RULELIST* rlist = (RULELIST*) calloc(1, sizeof(RULELIST));
                RULE* ruledef = (RULE*) calloc(1, sizeof(RULE));

                if (ruledef && rlist)
                {
                    ruledef->name = strdup(tok);
                    ruledef->type = RT_UNDEFINED;
                    ruledef->on_queries = QUERY_OP_UNDEFINED;
                    rlist->rule = ruledef;
                    rlist->next = instance->rules;
                    instance->rules = rlist;
                    rval = parse_rule_definition(instance, ruledef, rulestr, &saveptr);
                }
                else
                {
                    free(rlist);
                    free(ruledef);
                    MXS_ERROR("Memory allocation failed.");
                }
            }
            else
            {
                MXS_ERROR("dbfwfilter: Rule parsing failed, incomplete rule: %s", rule);
            }
        }
        else if (strcmp("users", tok) == 0)
        {
            /** Rules are applied to users after they have been parsed */
            add_users(rulestr, instance);
            rval = true;
        }
        else
        {
            MXS_ERROR("Unknown token in rule '%s': %s", rule, tok);
        }
    }
    else
    {
        MXS_ERROR("dbfwfilter: Rule parsing failed, no rule: %s", rule);
    }

    return rval;
}

bool is_comment(char* str)
{
    char *ptr = str;

    while (*ptr != '\0')
    {
        if (isspace(*ptr))
        {
            ptr++;
        }
        else if (*ptr == '#')
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    return true;
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
    STRLINK *ptr, *tmp;
    char *filename = NULL, *nl;
    char buffer[2048];
    FILE* file;
    bool err = false;

    if ((my_instance = calloc(1, sizeof(FW_INSTANCE))) == NULL ||
        (my_instance->lock = (SPINLOCK*) malloc(sizeof(SPINLOCK))) == NULL)
    {
        free(my_instance);
        MXS_ERROR("Memory allocation for firewall filter failed.");
        return NULL;
    }

    spinlock_init(my_instance->lock);

    if ((ht = hashtable_alloc(100, hashkeyfun, hashcmpfun)) == NULL)
    {
        MXS_ERROR("Unable to allocate hashtable.");
        free(my_instance);
        return NULL;
    }

    hashtable_memory_fns(ht, (HASHMEMORYFN) strdup, NULL, (HASHMEMORYFN) free, huserfree);

    my_instance->htable = ht;
    my_instance->def_op = true;
    my_instance->userstrings = NULL;
    my_instance->regflags = 0;

    for (i = 0; params[i]; i++)
    {
        if (strcmp(params[i]->name, "rules") == 0)
        {
            if (filename)
            {
                free(filename);
            }
            filename = strdup(params[i]->value);
        }
    }

    if (options)
    {
        for (i = 0; options[i]; i++)
        {
            if (strcmp(options[i], "ignorecase") == 0)
            {
                my_instance->regflags |= REG_ICASE;
            }
        }
    }

    if (filename == NULL)
    {
        MXS_ERROR("Unable to find rule file for firewall filter. Please provide the path with"
                  " rules=<path to file>");
        hashtable_free(my_instance->htable);
        free(my_instance);
        return NULL;
    }

    if ((file = fopen(filename, "rb")) == NULL)
    {
        MXS_ERROR("Error while opening rule file for firewall filter.");
        hashtable_free(my_instance->htable);
        free(my_instance);
        free(filename);
        return NULL;
    }


    bool file_empty = true;

    while (!feof(file))
    {

        if (fgets(buffer, 2048, file) == NULL)
        {
            if (ferror(file))
            {
                MXS_ERROR("Error while reading rule file for firewall filter.");
                fclose(file);
                hashtable_free(my_instance->htable);
                free(my_instance);
                return NULL;
            }

            if (feof(file))
            {
                break;
            }
        }

        if ((nl = strchr(buffer, '\n')) != NULL && ((char*) nl - (char*) buffer) < 2048)
        {
            *nl = '\0';
        }

        if (strnlen(buffer, 2048) < 1 || is_comment(buffer))
        {
            continue;
        }

        file_empty = false;

        if (!parse_rule(buffer, my_instance))
        {
            fclose(file);
            err = true;
            goto retblock;
        }
    }

    if (file_empty)
    {
        MXS_ERROR("dbfwfilter: File is empty: %s", filename);
        free(filename);
        err = true;
        goto retblock;
    }

    fclose(file);
    free(filename);

    /**Apply the rules to users*/
    ptr = my_instance->userstrings;

    if (ptr == NULL)
    {
        MXS_ERROR("dbfwfilter: No 'users' line found.");
        err = true;
        goto retblock;
    }

    while (ptr)
    {
        if (!link_rules(ptr->value, my_instance))
        {
            MXS_ERROR("dbfwfilter: Failed to parse rule: %s", ptr->value);
            err = true;
        }
        tmp = ptr;
        ptr = ptr->next;
        free(tmp->value);
        free(tmp);
    }

retblock:

    if (err)
    {
        hrulefree(my_instance->rules);
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
closeSession(FILTER *instance, void *session){ }

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
        session->session->data == NULL ||
        session->session->client == NULL)
    {
        MXS_ERROR("Firewall filter session missing data.");
        return NULL;
    }

    dcb = session->session->client;
    mysql_session = (MYSQL_session*) session->session->data;
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
                dcb->user, dcb->remote,  mysql_session->db);
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
bool rule_matches(FW_INSTANCE* my_instance, FW_SESSION* my_session, GWBUF *queue, USER* user, RULELIST *rulelist, char* query)
{
    char *ptr, *where, *msg = NULL;
    char emsg[512];

    unsigned char* memptr = (unsigned char*) queue->start;
    bool is_sql, is_real, matches;
    skygw_query_op_t optype = QUERY_OP_UNDEFINED;
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
        if (!query_is_parsed(queue))
        {
            parse_query(queue);
        }
        optype = query_classifier_get_operation(queue);
        is_real = skygw_is_real_query(queue);

    }

    if (rulelist->rule->on_queries == QUERY_OP_UNDEFINED || rulelist->rule->on_queries & optype)
    {
        switch (rulelist->rule->type)
        {
            case RT_UNDEFINED:
                MXS_ERROR("Undefined rule type found.");
                break;

            case RT_REGEX:
                if (query && regexec(rulelist->rule->data, query, 0, NULL, 0) == 0)
                {

                    matches = true;

                    if (!rulelist->rule->allow)
                    {
                        msg = strdup("Permission denied, query matched regular expression.");
                        MXS_INFO("dbfwfilter: rule '%s': regex matched on query", rulelist->rule->name);
                        goto queryresolved;
                    }
                    else
                    {
                        break;
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
                    where = skygw_get_affected_fields(queue);
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
                    where = skygw_get_affected_fields(queue);

                    if (where != NULL)
                    {
                        strptr = where;

                        if (strchr(strptr, '*'))
                        {

                            matches = true;
                            msg = strdup("Usage of wildcard denied.");
                            MXS_INFO("dbfwfilter: rule '%s': query contains a wildcard.", rulelist->rule->name);
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
                spinlock_acquire(my_instance->lock);
                rule_qs = (QUERYSPEED*) rulelist->rule->data;
                spinlock_release(my_instance->lock);

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

                        double blocked_for = queryspeed->cooldown - difftime(time_now, queryspeed->triggered);

                        sprintf(emsg, "Queries denied for %f seconds", blocked_for);
                        MXS_INFO("dbfwfilter: rule '%s': user denied for %f seconds", rulelist->rule->name, blocked_for);
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

                        MXS_INFO("dbfwfilter: rule '%s': query limit triggered (%d queries in %f seconds), "
                                 "denying queries from user for %f seconds.",
                                 rulelist->rule->name,
                                 queryspeed->limit,
                                 queryspeed->period,
                                 queryspeed->cooldown);
                        double blocked_for = queryspeed->cooldown - difftime(time_now, queryspeed->triggered);
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
                    !skygw_query_has_clause(queue))
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
bool check_match_any(FW_INSTANCE* my_instance, FW_SESSION* my_session, GWBUF *queue, USER* user)
{
    bool is_sql, rval = false;
    int qlen;
    char *fullquery = NULL, *ptr;
    unsigned char* memptr = (unsigned char*) queue->start;
    RULELIST* rulelist;
    is_sql = modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue);

    if (is_sql)
    {
        if (!query_is_parsed(queue))
        {
            parse_query(queue);
        }

        qlen = gw_mysql_get_byte3(memptr);
        qlen = qlen < 0xffffff ? qlen : 0xffffff;
        fullquery = malloc((qlen) * sizeof(char));
        memcpy(fullquery, memptr + 5, qlen - 1);
        memset(fullquery + qlen - 1, 0, 1);
    }

    if ((rulelist = user->rules_or) == NULL)
    {
        goto retblock;
    }

    while (rulelist)
    {

        if (!rule_is_active(rulelist->rule))
        {
            rulelist = rulelist->next;
            continue;
        }
        if ((rval = rule_matches(my_instance, my_session, queue, user, rulelist, fullquery)))
        {
            goto retblock;
        }

        rulelist = rulelist->next;
    }

retblock:

    free(fullquery);

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
bool check_match_all(FW_INSTANCE* my_instance, FW_SESSION* my_session, GWBUF *queue, USER* user, bool strict_all)
{
    bool is_sql, rval = true;
    bool have_active_rule = false;
    int qlen;
    unsigned char* memptr = (unsigned char*) queue->start;
    char *fullquery = NULL, *ptr;

    RULELIST* rulelist;
    is_sql = modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue);

    if (is_sql)
    {
        if (!query_is_parsed(queue))
        {
            parse_query(queue);
        }

        qlen = gw_mysql_get_byte3(memptr);
        qlen = qlen < 0xffffff ? qlen : 0xffffff;
        fullquery = malloc((qlen) * sizeof(char));
        memcpy(fullquery, memptr + 5, qlen - 1);
        memset(fullquery + qlen - 1, 0, 1);
    }

    if (strict_all)
    {
        rulelist = user->rules_strict_and;
    }
    else
    {
        rulelist = user->rules_and;
    }

    if (rulelist == NULL)
    {
        rval = false;
        goto retblock;
    }

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

retblock:
    free(fullquery);
    return rval;
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
    bool accept = my_instance->def_op;
    char *msg = NULL, *fullquery = NULL, *ipaddr;
    char uname_addr[128];
    DCB* dcb = my_session->session->client;
    USER* user = NULL;
    GWBUF* forward;
    ipaddr = strdup(dcb->remote);
    sprintf(uname_addr, "%s@%s", dcb->user, ipaddr);

    if (modutil_is_SQL(queue) && modutil_count_statements(queue) > 1)
    {
        if (my_session->errmsg)
        {
            free(my_session->errmsg);
        }
        my_session->errmsg = strdup("This filter does not support multi-statements.");
        accept = false;
        goto queryresolved;
    }

    if ((user = (USER*) hashtable_fetch(my_instance->htable, uname_addr)) == NULL)
    {
        while (user == NULL && next_ip_class(ipaddr))
        {
            sprintf(uname_addr, "%s@%s", dcb->user, ipaddr);
            user = (USER*) hashtable_fetch(my_instance->htable, uname_addr);
        }
    }

    if (user == NULL)
    {
        strcpy(ipaddr, dcb->remote);
        do
        {
            sprintf(uname_addr, "%%@%s", ipaddr);
            user = (USER*) hashtable_fetch(my_instance->htable, uname_addr);
        }
        while (user == NULL && next_ip_class(ipaddr));
    }

    if (user == NULL)
    {
        /**
         *No rules matched, do default operation.
         */

        goto queryresolved;
    }

    if (check_match_any(my_instance, my_session, queue, user))
    {
        accept = false;
        goto queryresolved;
    }

    if (check_match_all(my_instance, my_session, queue, user, false))
    {
        accept = false;
        goto queryresolved;
    }

    if (check_match_all(my_instance, my_session, queue, user, true))
    {
        accept = false;
        goto queryresolved;
    }

queryresolved:

    free(ipaddr);
    free(fullquery);

    if (accept)
    {
        return my_session->down.routeQuery(my_session->down.instance,
                                           my_session->down.session, queue);
    }
    else
    {
        gwbuf_free(queue);

        if (my_session->errmsg)
        {
            msg = my_session->errmsg;
        }
        forward = gen_dummy_error(my_session, msg);

        if (my_session->errmsg)
        {
            free(my_session->errmsg);
            my_session->errmsg = NULL;
        }
        return dcb->func.write(dcb, forward);
    }
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
    RULELIST* rules;
    int type;

    if (my_instance)
    {
        spinlock_acquire(my_instance->lock);
        rules = my_instance->rules;

        dcb_printf(dcb, "Firewall Filter\n");
        dcb_printf(dcb, "%-24s%-24s%-24s\n", "Rule", "Type", "Times Matched");
        while (rules)
        {
            if ((int) rules->rule->type > 0 &&
                (int) rules->rule->type < sizeof(rule_names) / sizeof(char**))
            {
                type = (int) rules->rule->type;
            }
            else
            {
                type = 0;
            }
            dcb_printf(dcb, "%-24s%-24s%-24d\n",
                       rules->rule->name,
                       rule_names[type],
                       rules->rule->times_matched);
            rules = rules->next;
        }
        spinlock_release(my_instance->lock);
    }
}

/**
 * Parse at_times rule.
 * @param tok Pointer to last token, should be a valid timerange
 * @param saveptr Pointer to the beginning of next token
 * @param ruledef The rule definition to which this at_times rule is applied
 * @return True if parsing was successful, false if an error occurred
 */
bool parse_at_times(const char** tok, char** saveptr, RULE* ruledef)
{
    TIMERANGE *tr = NULL;
    bool success = true;

    while (*tok && strcmp(*tok, "on_queries") != 0)
    {
        if (!check_time(*tok))
        {
            MXS_ERROR("dbfwfilter: Rule parsing failed, malformed time definition: %s", *tok);
            success = false;
            break;
        }

        TIMERANGE *tmp = parse_time(*tok);

        if (tmp == NULL)
        {
            MXS_ERROR("dbfwfilter: Rule parsing failed, unexpected characters after time definition.");
            success = false;
            tr_free(tr);
            break;
        }

        if (IS_RVRS_TIME(tmp))
        {
            tmp = split_reverse_time(tmp);
        }

        tmp->next = tr;
        tr = tmp;
        *tok = strtok_r(NULL, " ", saveptr);
    }

    if (success)
    {
        ruledef->active = tr;
    }

    return success;
}

/**
 * Parse limit_queries rule
 * @param instance Filter instance
 * @param ruledef Rule definition
 * @param saveptr Pointer to start of next token
 * @return True if parsing was successful and false if an error occurred.
 */
bool parse_limit_queries(FW_INSTANCE* instance, RULE* ruledef, const char* rule, char** saveptr)
{
    char *errptr = NULL;
    bool rval = false;
    QUERYSPEED* qs = NULL;
    const char *tok = strtok_r(NULL, " ", saveptr);

    if (tok == NULL)
    {
        MXS_ERROR("dbfwfilter: Missing parameter in limit_queries: '%s'.", rule);
        goto retblock;
    }

    qs = (QUERYSPEED*) calloc(1, sizeof(QUERYSPEED));

    if (qs == NULL)
    {
        MXS_ERROR("dbfwfilter: Memory allocation failed when parsing "
                  "'limit_queries' rule");
        goto retblock;
    }

    qs->limit = strtol(tok, &errptr, 0);

    if (errptr && *errptr != '\0')
    {
        MXS_ERROR("dbfwfilter: Rule parsing failed, not a number: '%s'.", tok);
        goto retblock;
    }

    if (qs->limit < 1)
    {
        MXS_ERROR("dbfwfilter: Bad query amount: %s", tok);
        goto retblock;
    }

    errptr = NULL;
    tok = strtok_r(NULL, " ", saveptr);

    if (tok == NULL)
    {
        MXS_ERROR("dbfwfilter: Missing parameter in limit_queries: '%s'.", rule);
        goto retblock;
    }

    qs->period = strtod(tok, &errptr);

    if (errptr && *errptr != '\0')
    {
        MXS_ERROR("dbfwfilter: Rule parsing failed, not a number: '%s'.", tok);
        goto retblock;
    }

    if (qs->period < 1)
    {
        MXS_ERROR("dbfwfilter: Bad time period: %s", tok);
        goto retblock;
    }

    errptr = NULL;
    tok = strtok_r(NULL, " ", saveptr);

    if (tok == NULL)
    {
        MXS_ERROR("dbfwfilter: Missing parameter in limit_queries: '%s'.", rule);
        goto retblock;
    }
    qs->cooldown = strtod(tok, &errptr);

    if (errptr && *errptr != '\0')
    {
        MXS_ERROR("dbfwfilter: Rule parsing failed, not a number: '%s'.", tok);
        goto retblock;
    }

    if (qs->cooldown < 1)
    {
        MXS_ERROR("dbfwfilter: Bad blocking period: %s", tok);
    }
    rval = true;

retblock:

    if (rval)
    {
        qs->id = atomic_add(&instance->idgen, 1);
        ruledef->type = RT_THROTTLE;
        ruledef->data = (void*) qs;
    }
    else
    {
        free(qs);
    }

    return rval;
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
