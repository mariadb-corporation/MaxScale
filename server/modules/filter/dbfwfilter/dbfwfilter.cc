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

#include "dbfwfilter.hh"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <sstream>

#include <maxscale/atomic.h>
#include <maxscale/modulecmd.h>
#include <maxscale/modutil.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/platform.h>
#include <maxscale/thread.h>
#include <maxscale/pcre2.h>
#include <maxscale/alloc.h>
#include <maxscale/spinlock.hh>

#include "rules.hh"
#include "user.hh"

MXS_BEGIN_DECLS
#include "ruleparser.yy.h"
#include "lex.yy.h"

/** Older versions of Bison don't include the parsing function in the header */
#ifndef dbfw_yyparse
int dbfw_yyparse(void*);
#endif
MXS_END_DECLS

/** The rules and users for each thread */
struct DbfwThread
{
    int        rule_version;
    RuleList   rules;
    UserMap    users;
};

thread_local DbfwThread* this_thread = NULL;

bool parse_at_times(const char** tok, char** saveptr, Rule* ruledef);
bool parse_limit_queries(Dbfw* instance, Rule* ruledef, const char* rule, char** saveptr);
static void rule_free_all(Rule* rule);
static bool process_rule_file(std::string filename, RuleList* rules, UserMap* users);
bool replace_rules(Dbfw* instance);

static void print_rule(Rule *rules, char *dest)
{
    sprintf(dest, "%s, %s, %d", rules->name().c_str(),
            rules->type().c_str(), rules->times_matched);
}

static json_t* rule_to_json(const SRule& rule)
{
    json_t* rval = json_object();

    json_object_set_new(rval, "name", json_string(rule->name().c_str()));
    json_object_set_new(rval, "type", json_string(rule->type().c_str()));
    json_object_set_new(rval, "times_matched", json_integer(rule->times_matched));

    return rval;
}

static json_t* rules_to_json(const RuleList& rules)
{
    json_t* rval = json_array();

    for (RuleList::const_iterator it = this_thread->rules.begin(); it != this_thread->rules.end(); it++)
    {
        const SRule& rule = *it;
        json_array_append_new(rval, rule_to_json(rule));
    }

    return rval;
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
bool parse_querytypes(const char* str, SRule rule)
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
                rule->on_queries |= FW_OP_SELECT;
            }
            else if (strcmp(buffer, "insert") == 0)
            {
                rule->on_queries |= FW_OP_INSERT;
            }
            else if (strcmp(buffer, "update") == 0)
            {
                rule->on_queries |= FW_OP_UPDATE;
            }
            else if (strcmp(buffer, "delete") == 0)
            {
                rule->on_queries |= FW_OP_DELETE;
            }
            else if (strcmp(buffer, "use") == 0)
            {
                rule->on_queries |= FW_OP_CHANGE_DB;
            }
            else if (strcmp(buffer, "grant") == 0)
            {
                rule->on_queries |= FW_OP_GRANT;
            }
            else if (strcmp(buffer, "revoke") == 0)
            {
                rule->on_queries |= FW_OP_REVOKE;
            }
            else if (strcmp(buffer, "drop") == 0)
            {
                rule->on_queries |= FW_OP_DROP;
            }
            else if (strcmp(buffer, "create") == 0)
            {
                rule->on_queries |= FW_OP_CREATE;
            }
            else if (strcmp(buffer, "alter") == 0)
            {
                rule->on_queries |= FW_OP_ALTER;
            }
            else if (strcmp(buffer, "load") == 0)
            {
                rule->on_queries |= FW_OP_LOAD;
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

bool dbfw_reload_rules(const MODULECMD_ARG *argv, json_t** output)
{
    MXS_FILTER_DEF *filter = argv->argv[0].value.filter;
    Dbfw *inst = (Dbfw*)filter_def_get_instance(filter);
    std::string filename = inst->get_rule_file();

    if (modulecmd_arg_is_present(argv, 1))
    {
        /** We need to change the rule file */
        filename = argv->argv[1].value.string;
    }

    return inst->reload_rules(filename);
}

bool dbfw_show_rules(const MODULECMD_ARG *argv, json_t** output)
{
    DCB *dcb = argv->argv[0].value.dcb;
    MXS_FILTER_DEF *filter = argv->argv[1].value.filter;
    Dbfw *inst = (Dbfw*)filter_def_get_instance(filter);

    dcb_printf(dcb, "Rule, Type, Times Matched\n");

    if (this_thread->rules.empty() || this_thread->users.empty())
    {
        if (!replace_rules(inst))
        {
            return 0;
        }
    }

    for (RuleList::const_iterator it = this_thread->rules.begin(); it != this_thread->rules.end(); it++)
    {
        const SRule& rule = *it;
        char buf[rule->name().length() + 200]; // Some extra space
        print_rule(rule.get(), buf);
        dcb_printf(dcb, "%s\n", buf);
    }

    return true;
}

bool dbfw_show_rules_json(const MODULECMD_ARG *argv, json_t** output)
{
    MXS_FILTER_DEF *filter = argv->argv[0].value.filter;
    Dbfw *inst = (Dbfw*)filter_def_get_instance(filter);

    json_t* arr = json_array();

    if (this_thread->rules.empty() || this_thread->users.empty())
    {
        if (!replace_rules(inst))
        {
            return 0;
        }
    }

    for (RuleList::const_iterator it = this_thread->rules.begin(); it != this_thread->rules.end(); it++)
    {
        const SRule& rule = *it;
        json_array_append_new(arr, rule_to_json(rule));
    }

    *output = arr;
    return true;
}

static int dbfw_thr_init()
{
    int rval = 0;

    if ((this_thread = new (std::nothrow) DbfwThread) == NULL)
    {
        MXS_OOM();
        rval = -1;
    }

    return rval;
}

static void dbfw_thr_finish()
{
    MXS_EXCEPTION_GUARD(delete this_thread);
}

static const MXS_ENUM_VALUE action_values[] =
{
    {"allow",  FW_ACTION_ALLOW},
    {"block",  FW_ACTION_BLOCK},
    {"ignore", FW_ACTION_IGNORE},
    {NULL}
};

MXS_BEGIN_DECLS

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

    modulecmd_register_command(MXS_MODULE_NAME, "rules/reload", MODULECMD_TYPE_ACTIVE,
                               dbfw_reload_rules, 2, args_rules_reload,
                               "Reload dbfwfilter rules");

    modulecmd_arg_type_t args_rules_show[] =
    {
        {MODULECMD_ARG_OUTPUT, "DCB where result is written"},
        {MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Filter to inspect"}
    };

    modulecmd_register_command(MXS_MODULE_NAME, "rules", MODULECMD_TYPE_PASSIVE,
                               dbfw_show_rules, 2, args_rules_show,
                               "(deprecated) Show dbfwfilter rule statistics");

    modulecmd_arg_type_t args_rules_show_json[] =
    {
        {MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Filter to inspect"}
    };

    modulecmd_register_command(MXS_MODULE_NAME, "rules/json", MODULECMD_TYPE_PASSIVE,
                               dbfw_show_rules_json, 1, args_rules_show_json,
                               "Show dbfwfilter rule statistics as JSON");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "Firewall Filter",
        "V1.2.0",
        RCAP_TYPE_STMT_INPUT,
        &Dbfw::s_object,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        dbfw_thr_init, /* Thread init. */
        dbfw_thr_finish, /* Thread finish. */
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

MXS_END_DECLS

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
    RuleList rule;
    ValueList user;
    ValueList active_rules;
    enum match_type active_mode;
    TemplateList templates;
    ValueList values;
    ValueList auxiliary_values;
    std::string name;

    /** Helper function for adding rules */
    void add(Rule* value)
    {
        rule.push_front(SRule(value));
        values.clear();
        auxiliary_values.clear();
    }
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
static SRule find_rule_by_name(const RuleList& rules, std::string name)
{
  for (RuleList::const_iterator it = rules.begin(); it != rules.end(); it++)
    {
        if ((*it)->name() == name)
        {
            return *it;
        }
    }

    return SRule();
}

bool set_rule_name(void* scanner, char* name)
{
    bool rval = true;
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t)scanner);
    ss_dassert(rstack);

    if (find_rule_by_name(rstack->rule, name))
    {
        MXS_ERROR("Redefinition of rule '%s' on line %d.", name, dbfw_yyget_lineno(scanner));
        rval = false;
    }
    else
    {
        rstack->name = name;
    }

    return rval;
}

/**
 * Remove backticks from a string
 * @param string String to parse
 * @return String without backticks
 */
static std::string strip_backticks(std::string str)
{
    size_t start = str.find_first_of('`');
    size_t end = str.find_last_of('`');

    if (end != std::string::npos && start != std::string::npos)
    {
        str = str.substr(start + 1, (end - 1) - (start + 1));
    }

    return str;
}

void push_value(void* scanner, char* value)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t)scanner);
    ss_dassert(rstack);
    rstack->values.push_back(strip_backticks(value));
}

void push_auxiliary_value(void* scanner, char* value)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t)scanner);
    ss_dassert(rstack);
    rstack->auxiliary_values.push_back(strip_backticks(value));
}

/**
 * Add a user to the current rule linking expression
 * @param scanner Current scanner
 * @param name Name of the user
 */
void add_active_user(void* scanner, const char* name)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->user.push_back(name);
}

/**
 * Add a rule to the current rule linking expression
 * @param scanner Current scanner
 * @param name Name of the rule
 */
void add_active_rule(void* scanner, const char* name)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->active_rules.push_back(name);
}

/**
 * Add an optional at_times definition to the rule
 * @param scanner Current scanner
 * @param range two ISO-8601 compliant times separated by a single dash
 */
bool add_at_times_rule(void* scanner, const char* range)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    ss_dassert(!rstack->rule.empty());
    TIMERANGE* timerange = parse_time(range);
    ss_dassert(timerange);

    if (timerange)
    {
        timerange->next = rstack->rule.front()->active;
        rstack->rule.front()->active = timerange;
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
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    ss_dassert(!rstack->rule.empty());
    parse_querytypes(sql, rstack->rule.front());
}

/**
 * Link users and rules
 * @param scanner Current scanner
 */
bool create_user_templates(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);

    for (ValueList::const_iterator it = rstack->user.begin(); it != rstack->user.end(); it++)
    {
        SUserTemplate newtemp = SUserTemplate(new UserTemplate(*it, rstack->active_rules, rstack->active_mode));
        rstack->templates.push_back(newtemp);
    }

    rstack->user.clear();
    rstack->active_rules.clear();

    return true;
}

void set_matching_mode(void* scanner, enum match_type mode)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->active_mode = mode;
}

/**
 * Define the current rule as a basic permission rule that always matches
 *
 * @param scanner Current scanner
 */
void define_basic_rule(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new Rule(rstack->name));
}

/**
 * Define the topmost rule as a wildcard rule
 *
 * @param scanner Current scanner
 */
void define_wildcard_rule(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new WildCardRule(rstack->name));
}

/**
 * Define the current rule as a columns rule
 *
 * @param scanner Current scanner
 */
void define_columns_rule(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new ColumnsRule(rstack->name, rstack->values));
}

/**
 * Define the current rule as a function rule
 *
 * @param scanner Current scanner
 */
void define_function_rule(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new FunctionRule(rstack->name, rstack->values));
}

/**
 * Define the current rule as a function usage rule
 *
 * @param scanner Current scanner
 */
void define_function_usage_rule(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new FunctionUsageRule(rstack->name, rstack->values));
}

/**
 * Define the current rule as a function rule
 *
 * @param scanner Current scanner
 */
void define_column_function_rule(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new ColumnFunctionRule(rstack->name, rstack->values, rstack->auxiliary_values));
}

/**
 * Define the topmost rule as a no_where_clause rule
 *
 * @param scanner Current scanner
 */
void define_where_clause_rule(void* scanner)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new NoWhereClauseRule(rstack->name));
}

/**
 * Define the topmost rule as a no_where_clause rule
 *
 * @param scanner    Current scanner
 * @param max        Maximum amount of queries inside a time window
 * @param timeperiod The time window during which the queries are counted
 * @param holdoff    The number of seconds queries are blocked after the limit is exceeded
 */
void define_limit_queries_rule(void* scanner, int max, int timeperiod, int holdoff)
{
    struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
    ss_dassert(rstack);
    rstack->add(new LimitQueriesRule(rstack->name, max, timeperiod, holdoff));
}

/**
 * Define the topmost rule as a regex rule
 *
 * @param scanner Current scanner
 * @param pattern Quoted regex pattern
 *
 * @return True if the regex pattern was valid
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
        struct parser_stack* rstack = (struct parser_stack*)dbfw_yyget_extra((yyscan_t) scanner);
        ss_dassert(rstack);
        rstack->add(new RegexRule(rstack->name, re));
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
static bool process_user_templates(UserMap& users, const TemplateList& templates,
                                   RuleList& rules)
{
    bool rval = true;

    if (templates.size() == 0)
    {
        MXS_ERROR("No user definitions found in the rule file.");
        rval = false;
    }

    for (TemplateList::const_iterator it = templates.begin(); it != templates.end(); it++)
    {
        const SUserTemplate& ut = *it;

        if (users.find(ut->name) == users.end())
        {
            users[ut->name] = SUser(new User(ut->name));
        }

        SUser& user = users[ut->name];
        RuleList newrules;

        for (ValueList::const_iterator r_it = ut->rulenames.begin();
             r_it != ut->rulenames.end(); r_it++)
        {
            SRule rule = find_rule_by_name(rules, r_it->c_str());

            if (rule)
            {
                newrules.push_front(rule);
            }
            else
            {
                MXS_ERROR("Could not find definition for rule '%s'.", r_it->c_str());
                rval = false;
            }
        }

        if (newrules.size() > 0)
        {
            user->append_rules(ut->type, newrules);
        }
    }

    return rval;
}

/**
 * Read a rule file from disk and process it into rule and user definitions
 * @param filename Name of the file
 * @param instance Filter instance
 * @return True on success, false on error.
 */
static bool do_process_rule_file(const char* filename, RuleList* rules, UserMap* users)
{
    int rc = 1;
    FILE *file = fopen(filename, "r");

    if (file)
    {
        yyscan_t scanner;
        struct parser_stack pstack;

        dbfw_yylex_init(&scanner);
        YY_BUFFER_STATE buf = dbfw_yy_create_buffer(file, YY_BUF_SIZE, scanner);
        dbfw_yyset_extra(&pstack, scanner);
        dbfw_yy_switch_to_buffer(buf, scanner);

        /** Parse the rule file */
        rc = dbfw_yyparse(scanner);

        dbfw_yy_delete_buffer(buf, scanner);
        dbfw_yylex_destroy(scanner);
        fclose(file);
        UserMap new_users;

        if (rc == 0 && process_user_templates(new_users, pstack.templates, pstack.rule))
        {
            rules->swap(pstack.rule);
            users->swap(new_users);
        }
        else
        {
            rc = 1;
            MXS_ERROR("Failed to process rule file '%s'.", filename);
        }
    }
    else
    {
        MXS_ERROR("Failed to open rule file '%s': %d, %s", filename, errno,
                  mxs_strerror(errno));

    }

    return rc == 0;
}

static bool process_rule_file(std::string filename, RuleList* rules, UserMap* users)
{
    bool rval = false;
    MXS_EXCEPTION_GUARD(rval = do_process_rule_file(filename.c_str(), rules, users));
    return rval;
}

/**
 * @brief Replace the rule file used by this thread
 *
 * This function replaces or initializes the thread local list of rules and users.
 *
 * @param instance Filter instance
 * @return True if the session can continue, false on fatal error.
 */
bool replace_rules(Dbfw* instance)
{
    bool rval = true;
    std::string filename = instance->get_rule_file();
    RuleList rules;
    UserMap  users;

    if (process_rule_file(filename, &rules, &users))
    {
        this_thread->rules.swap(rules);
        this_thread->users.swap(users);
        rval = true;
    }
    else if (!this_thread->rules.empty() && !this_thread->users.empty())
    {
        MXS_ERROR("Failed to parse rules at '%s'. Old rules are still used.",
                  filename.c_str());
    }
    else
    {
        MXS_ERROR("Failed to parse rules at '%s'. No previous rules available, "
                  "closing session.", filename.c_str());
        rval = false;
    }

    return rval;
}

static bool update_rules(Dbfw* my_instance)
{
    bool rval = true;
    int rule_version = my_instance->get_rule_version();

    if (this_thread->rule_version < rule_version)
    {
        if (!replace_rules(my_instance))
        {
            rval = false;
        }

        this_thread->rule_version = rule_version;
    }

    return rval;
}

Dbfw::Dbfw(MXS_CONFIG_PARAMETER* params):
    m_action((enum fw_actions)config_get_enum(params, "action", action_values)),
    m_log_match(0),
    m_lock(SPINLOCK_INIT),
    m_filename(config_get_string(params, "rules")),
    m_version(1)
{
    if (config_get_bool(params, "log_match"))
    {
        m_log_match |= FW_LOG_MATCH;
    }

    if (config_get_bool(params, "log_no_match"))
    {
        m_log_match |= FW_LOG_NO_MATCH;
    }
}

Dbfw::~Dbfw()
{

}

Dbfw* Dbfw::create(const char* zName, char** pzOptions, MXS_CONFIG_PARAMETER* pParams)
{
    Dbfw* rval = NULL;
    RuleList rules;
    UserMap  users;
    std::string file = config_get_string(pParams, "rules");

    if (process_rule_file(file, &rules, &users))
    {
        rval = new (std::nothrow) Dbfw(pParams);
    }

    return rval;
}

DbfwSession* Dbfw::newSession(MXS_SESSION* session)
{
    return new (std::nothrow) DbfwSession(this, session);
}

fw_actions Dbfw::get_action() const
{
    return m_action;
}

int Dbfw::get_log_bitmask() const
{
    return m_log_match;
}

std::string Dbfw::get_rule_file() const
{
    mxs::SpinLockGuard guard(m_lock);
    return m_filename;
}

int Dbfw::get_rule_version() const
{
    return atomic_load_int32(&m_version);
}

bool Dbfw::do_reload_rules(std::string filename)
{
    RuleList rules;
    UserMap  users;
    bool rval = false;

    if (access(filename.c_str(), R_OK) == 0)
    {
        if (process_rule_file(filename, &rules, &users))
        {
            rval = true;
            m_filename = filename;
            atomic_add(&m_version, 1);
            MXS_NOTICE("Reloaded rules from: %s", filename.c_str());
        }
        else
        {
            modulecmd_set_error("Failed to process rule file '%s'. See log "
                                "file for more details.", filename.c_str());
        }
    }
    else
    {
        modulecmd_set_error("Failed to read rules at '%s': %d, %s", filename.c_str(),
                            errno, mxs_strerror(errno));
    }

    return rval;
}

bool Dbfw::reload_rules(std::string filename)
{
    mxs::SpinLockGuard guard(m_lock);
    return do_reload_rules(filename);
}

/**
 * Retrieve the user specific data for this session
 *
 * @param users Map containing the user data
 * @param name Username
 * @param remote Remove network address
 * @return The user data or NULL if it was not found
 */
static SUser find_user_data(const UserMap& users, std::string name, std::string remote)
{
    char nameaddr[name.length() + remote.length() + 2];
    snprintf(nameaddr, sizeof(nameaddr), "%s@%s", name.c_str(), remote.c_str());
    UserMap::const_iterator it = users.find(nameaddr);

    if (it == users.end())
    {
        char *ip_start = strchr(nameaddr, '@') + 1;
        while (it == users.end() && next_ip_class(ip_start))
        {
            it = users.find(nameaddr);
        }

        if (it == users.end())
        {
            snprintf(nameaddr, sizeof(nameaddr), "%%@%s", remote.c_str());
            ip_start = strchr(nameaddr, '@') + 1;

            while (it == users.end() && next_ip_class(ip_start))
            {
                it = users.find(nameaddr);
            }
        }
    }

    return it != users.end() ? it->second : SUser();
}

static bool command_is_mandatory(const GWBUF *buffer)
{
    switch (MYSQL_GET_COMMAND((uint8_t*)GWBUF_DATA(buffer)))
    {
    case MYSQL_COM_QUIT:
    case MYSQL_COM_PING:
    case MYSQL_COM_CHANGE_USER:
    case MYSQL_COM_SET_OPTION:
    case MYSQL_COM_FIELD_LIST:
    case MYSQL_COM_PROCESS_KILL:
    case MYSQL_COM_PROCESS_INFO:
        return true;

    default:
        return false;
    }
}

static std::string get_sql(GWBUF* buffer)
{
    std::string rval;
    char *sql;
    int len;

    if (modutil_extract_SQL(buffer, &sql, &len))
    {
        len = MXS_MIN(len, FW_MAX_SQL_LEN);
        rval.assign(sql, len);
    }

    return rval;
}

DbfwSession::DbfwSession(Dbfw* instance, MXS_SESSION* session):
    mxs::FilterSession::FilterSession(session),
    m_instance(instance),
    m_session(session)
{
}

DbfwSession::~DbfwSession()
{
}

void DbfwSession::set_error(const char* error)
{
    if (error)
    {
        m_error = error;
    }
}

std::string DbfwSession::get_error()const
{
    return m_error;
}

void DbfwSession::clear_error()
{
    m_error.clear();
}

std::string DbfwSession::user() const
{
    return m_session->client_dcb->user;
}

std::string DbfwSession::remote() const
{
    return m_session->client_dcb->remote;
}

QuerySpeed* DbfwSession::query_speed()
{
    return &m_qs;
}

fw_actions DbfwSession::get_action() const
{
    return m_instance->get_action();
}

int DbfwSession::send_error()
{
    ss_dassert(m_session && m_session->client_dcb);
    DCB* dcb = m_session->client_dcb;
    const char* db = mxs_mysql_get_current_db(m_session);
    std::stringstream ss;
    ss << "Access denied for user '" << user() << "'@'" << remote() << "'";

    if (db[0])
    {
        ss << " to database '" << db << "'";
    }

    if (m_error.length())
    {
        ss << ": " << m_error;
        clear_error();
    }

    return dcb->func.write(dcb, modutil_create_mysql_err_msg(1, 0, 1141,
                                                             "HY000", ss.str().c_str()));
}

int DbfwSession::routeQuery(GWBUF* buffer)
{
    int rval = 0;
    uint32_t type = 0;

    if (!update_rules(m_instance))
    {
        return rval;
    }

    if (modutil_is_SQL(buffer) || modutil_is_SQL_prepare(buffer))
    {
        type = qc_get_type_mask(buffer);
    }

    if (modutil_is_SQL(buffer) && modutil_count_statements(buffer) > 1)
    {
        set_error("This filter does not support multi-statements.");
        rval = send_error();
        gwbuf_free(buffer);
    }
    else
    {
        GWBUF* analyzed_queue = buffer;

        // QUERY_TYPE_PREPARE_STMT need not be handled separately as the
        // information about statements in COM_STMT_PREPARE packets is
        // accessed exactly like the information of COM_QUERY packets. However,
        // with named prepared statements in COM_QUERY packets, we need to take
        // out the preparable statement and base our decisions on that.

        if (qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT))
        {
            analyzed_queue = qc_get_preparable_stmt(buffer);
            ss_dassert(analyzed_queue);
        }

        SUser suser = find_user_data(this_thread->users, user(), remote());
        bool query_ok = false;

        if (command_is_mandatory(buffer))
        {
            query_ok = true;
        }
        else if (suser)
        {
            char* rname = NULL;
            bool match = suser->match(m_instance, this, analyzed_queue, &rname);

            switch (m_instance->get_action())
            {
            case FW_ACTION_ALLOW:
                query_ok = match;
                break;

            case FW_ACTION_BLOCK:
                query_ok = !match;
                break;

            case FW_ACTION_IGNORE:
                query_ok = true;
                break;

            default:
                MXS_ERROR("Unknown dbfwfilter action: %d", m_instance->get_action());
                ss_dassert(false);
                break;
            }

            if (m_instance->get_log_bitmask() != FW_LOG_NONE)
            {
                if (match && m_instance->get_log_bitmask() & FW_LOG_MATCH)
                {
                    MXS_NOTICE("[%s] Rule '%s' for '%s' matched by %s@%s: %s",
                               m_session->service->name, rname, suser->name(),
                               user().c_str(), remote().c_str(), get_sql(buffer).c_str());
                }
                else if (!match && m_instance->get_log_bitmask() & FW_LOG_NO_MATCH)
                {
                    MXS_NOTICE("[%s] Query for '%s' by %s@%s was not matched: %s",
                               m_session->service->name, suser->name(), user().c_str(),
                               remote().c_str(), get_sql(buffer).c_str());
                }
            }

            MXS_FREE(rname);
        }
        /** If the instance is in whitelist mode, only users that have a rule
         * defined for them are allowed */
        else if (m_instance->get_action() != FW_ACTION_ALLOW)
        {
            query_ok = true;
        }

        if (query_ok)
        {
            rval = mxs::FilterSession::routeQuery(buffer);
        }
        else
        {
            rval = send_error();
            gwbuf_free(buffer);
        }
    }

    return rval;
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

    return to_before > 0.0 && to_after < 0.0;
}

/**
 * Checks for active timeranges for a given rule.
 * @param rule Pointer to a RULE object
 * @return true if the rule is active
 */
bool rule_is_active(SRule rule)
{
    bool rval = true;

    if (rule->active)
    {
        rval = false;

        for (TIMERANGE* times = rule->active; times; times = times->next)
        {
            if (inside_timerange(times))
            {
                rval = true;
                break;
            }
        }
    }

    return rval;
}

/**
 * A convenience wrapper for snprintf and strdup
 *
 * @param format Format string
 * @param ...    Variable argument list
 *
 * @return Pointer to newly allocated and formatted string
 */
char* create_error(const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    int message_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    char* rval = (char*)MXS_MALLOC(message_len + 1);
    MXS_ABORT_IF_NULL(rval);

    va_start(valist, format);
    vsnprintf(rval, message_len + 1, format, valist);
    va_end(valist);

    return rval;
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
static char* create_parse_error(Dbfw* my_instance,
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

    if ((my_instance->get_action() == FW_ACTION_ALLOW) || (my_instance->get_action() == FW_ACTION_BLOCK))
    {
        msg = create_error("%s.", message);

        if (my_instance->get_action() == FW_ACTION_ALLOW)
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

/**
 * Check if a query matches a single rule
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param rule The rule to check
 * @param query Pointer to the null-terminated query string
 * @return true if the query matches the rule
 */
bool rule_matches(Dbfw* my_instance,
                  DbfwSession* my_session,
                  GWBUF *queue,
                  SRule rule,
                  char* query)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(queue));
    char *msg = NULL;
    bool matches = false;
    bool is_sql = modutil_is_SQL(queue) || modutil_is_SQL_prepare(queue);

    if (is_sql)
    {
        qc_parse_result_t parse_result = qc_parse(queue, QC_COLLECT_ALL);

        if (parse_result == QC_QUERY_INVALID)
        {
            msg = create_parse_error(my_instance, "tokenized", query, &matches);
        }
        else if (parse_result != QC_QUERY_PARSED && rule->need_full_parsing(queue))
        {
            msg = create_parse_error(my_instance, "parsed completely", query, &matches);
        }
    }

    if (msg == NULL && rule->matches_query_type(queue))
    {
        if ((matches = rule->matches_query(my_session, queue, &msg)))
        {
            rule->times_matched++;
        }
    }

    my_session->set_error(msg);
    MXS_FREE(msg);

    return matches;
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
void Dbfw::diagnostics(DCB *dcb) const
{
    dcb_printf(dcb, "Firewall Filter\n");
    dcb_printf(dcb, "Rule, Type, Times Matched\n");

    for (RuleList::const_iterator it = this_thread->rules.begin(); it != this_thread->rules.end(); it++)
    {
        const SRule& rule = *it;
        char buf[rule->name().length() + 200];
        print_rule(rule.get(), buf);
        dcb_printf(dcb, "%s\n", buf);
    }
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
json_t* Dbfw::diagnostics_json() const
{
    return rules_to_json(this_thread->rules);
}
