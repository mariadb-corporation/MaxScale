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

#define MXS_MODULE_NAME "cache"
#include "rules.h"

#include <errno.h>
#include <stdio.h>
#include <new>
#include <vector>

#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/query_classifier.h>
#include <maxscale/session.h>

#include "cachefilter.h"

static int next_thread_id = 0;
static thread_local int current_thread_id = -1;

inline int get_current_thread_id()
{
    if (current_thread_id == -1)
    {
        current_thread_id = atomic_add(&next_thread_id, 1);
    }

    return current_thread_id;
}

static const char KEY_ATTRIBUTE[] = "attribute";
static const char KEY_OP[] = "op";
static const char KEY_STORE[] = "store";
static const char KEY_USE[] = "use";
static const char KEY_VALUE[] = "value";

static const char VALUE_ATTRIBUTE_COLUMN[] = "column";
static const char VALUE_ATTRIBUTE_DATABASE[] = "database";
static const char VALUE_ATTRIBUTE_QUERY[] = "query";
static const char VALUE_ATTRIBUTE_TABLE[] = "table";
static const char VALUE_ATTRIBUTE_USER[] = "user";

static const char VALUE_OP_EQ[] = "=";
static const char VALUE_OP_NEQ[] = "!=";
static const char VALUE_OP_LIKE[] = "like";
static const char VALUE_OP_UNLIKE[] = "unlike";

struct cache_attribute_mapping
{
    const char*            name;
    cache_rule_attribute_t value;
};

static struct cache_attribute_mapping cache_store_attributes[] =
{
    {VALUE_ATTRIBUTE_COLUMN,   CACHE_ATTRIBUTE_COLUMN                },
    {VALUE_ATTRIBUTE_DATABASE, CACHE_ATTRIBUTE_DATABASE              },
    {VALUE_ATTRIBUTE_QUERY,    CACHE_ATTRIBUTE_QUERY                 },
    {VALUE_ATTRIBUTE_TABLE,    CACHE_ATTRIBUTE_TABLE                 },
    {NULL,                     static_cast<cache_rule_attribute_t>(0)}
};

static struct cache_attribute_mapping cache_use_attributes[] =
{
    {VALUE_ATTRIBUTE_USER, CACHE_ATTRIBUTE_USER                  },
    {NULL,                 static_cast<cache_rule_attribute_t>(0)}
};

static bool cache_rule_attribute_get(struct cache_attribute_mapping* mapping,
                                     const char* s,
                                     cache_rule_attribute_t* attribute);

static bool cache_rule_op_get(const char* s, cache_rule_op_t* op);

static bool        cache_rule_compare(CACHE_RULE* rule, int thread_id, const char* value);
static bool        cache_rule_compare_n(CACHE_RULE* rule, int thread_id, const char* value, size_t length);
static CACHE_RULE* cache_rule_create_regexp(cache_rule_attribute_t attribute,
                                            cache_rule_op_t op,
                                            const char* value,
                                            uint32_t debug);
static CACHE_RULE* cache_rule_create_simple(cache_rule_attribute_t attribute,
                                            cache_rule_op_t op,
                                            const char* value,
                                            uint32_t debug);
static CACHE_RULE* cache_rule_create_simple_ctd(cache_rule_attribute_t attribute,
                                                cache_rule_op_t op,
                                                const char* cvalue,
                                                uint32_t debug);
static CACHE_RULE* cache_rule_create_simple_user(cache_rule_attribute_t attribute,
                                                 cache_rule_op_t op,
                                                 const char* cvalue,
                                                 uint32_t debug);
static CACHE_RULE* cache_rule_create_simple_query(cache_rule_attribute_t attribute,
                                                  cache_rule_op_t op,
                                                  const char* cvalue,
                                                  uint32_t debug);
static CACHE_RULE* cache_rule_create(cache_rule_attribute_t attribute,
                                     cache_rule_op_t op,
                                     const char* value,
                                     uint32_t debug);
static bool cache_rule_matches_column_regexp(CACHE_RULE* rule,
                                             int thread_id,
                                             const char* default_db,
                                             const GWBUF* query);
static bool cache_rule_matches_column_simple(CACHE_RULE* rule,
                                             const char* default_db,
                                             const GWBUF* query);
static bool cache_rule_matches_column(CACHE_RULE* rule,
                                      int thread_id,
                                      const char* default_db,
                                      const GWBUF* query);
static bool cache_rule_matches_database(CACHE_RULE* rule,
                                        int thread_id,
                                        const char* default_db,
                                        const GWBUF* query);
static bool cache_rule_matches_query(CACHE_RULE* rule,
                                     int thread_id,
                                     const char* default_db,
                                     const GWBUF* query);
static bool cache_rule_matches_table(CACHE_RULE* rule,
                                     int thread_id,
                                     const char* default_db,
                                     const GWBUF* query);
static bool cache_rule_matches_table_regexp(CACHE_RULE* rule,
                                            int thread_id,
                                            const char* default_db,
                                            const GWBUF* query);
static bool cache_rule_matches_table_simple(CACHE_RULE* rule,
                                            const char* default_db,
                                            const GWBUF* query);
static bool cache_rule_matches_user(CACHE_RULE* rule, int thread_id, const char* user);
static bool cache_rule_matches(CACHE_RULE* rule,
                               int thread_id,
                               const char* default_db,
                               const GWBUF* query);

static void cache_rule_free(CACHE_RULE* rule);

static void         cache_rules_add_store_rule(CACHE_RULES* self, CACHE_RULE* rule);
static void         cache_rules_add_use_rule(CACHE_RULES* self, CACHE_RULE* rule);
static CACHE_RULES* cache_rules_create_from_json(json_t* root, uint32_t debug);
static bool         cache_rules_create_from_json(json_t* root,
                                                 uint32_t debug,
                                                 CACHE_RULES*** ppRules,
                                                 int32_t* pnRules);
static bool cache_rules_parse_json(CACHE_RULES* self, json_t* root);

typedef bool (* cache_rules_parse_element_t)(CACHE_RULES* self, json_t* object, size_t index);

static bool cache_rules_parse_array(CACHE_RULES * self, json_t* store, const char* name,
                                    cache_rules_parse_element_t);
static bool cache_rules_parse_store_element(CACHE_RULES* self, json_t* object, size_t index);
static bool cache_rules_parse_use_element(CACHE_RULES* self, json_t* object, size_t index);

static pcre2_match_data** alloc_match_datas(int count, pcre2_code* code);
static void               free_match_datas(int count, pcre2_match_data** datas);

/*
 * API begin
 */

const char* cache_rule_attribute_to_string(cache_rule_attribute_t attribute)
{
    switch (attribute)
    {
    case CACHE_ATTRIBUTE_COLUMN:
        return "column";

    case CACHE_ATTRIBUTE_DATABASE:
        return "database";

    case CACHE_ATTRIBUTE_QUERY:
        return "query";

    case CACHE_ATTRIBUTE_TABLE:
        return "table";

    case CACHE_ATTRIBUTE_USER:
        return "user";

    default:
        mxb_assert(!true);
        return "<invalid>";
    }
}

const char* cache_rule_op_to_string(cache_rule_op_t op)
{
    switch (op)
    {
    case CACHE_OP_EQ:
        return "=";

    case CACHE_OP_NEQ:
        return "!=";

    case CACHE_OP_LIKE:
        return "like";

    case CACHE_OP_UNLIKE:
        return "unlike";

    default:
        mxb_assert(!true);
        return "<invalid>";
    }
}

CACHE_RULES* cache_rules_create(uint32_t debug)
{
    CACHE_RULES* rules = (CACHE_RULES*)MXS_CALLOC(1, sizeof(CACHE_RULES));

    if (rules)
    {
        rules->debug = debug;
    }

    return rules;
}

bool cache_rules_load(const char* zPath,
                      uint32_t debug,
                      CACHE_RULES*** pppRules,
                      int32_t* pnRules)
{
    bool rv = false;

    *pppRules = NULL;
    *pnRules = 0;

    FILE* pF = fopen(zPath, "r");

    if (pF)
    {
        json_error_t error;
        json_t* pRoot = json_loadf(pF, JSON_DISABLE_EOF_CHECK, &error);

        if (pRoot)
        {
            rv = cache_rules_create_from_json(pRoot, debug, pppRules, pnRules);

            if (!rv)
            {
                json_decref(pRoot);
            }
        }
        else
        {
            MXS_ERROR("Loading rules file failed: (%s:%d:%d): %s",
                      zPath,
                      error.line,
                      error.column,
                      error.text);
        }

        fclose(pF);
    }
    else
    {
        MXS_ERROR("Could not open rules file %s for reading: %s",
                  zPath,
                  mxs_strerror(errno));
    }

    return rv;
}

bool cache_rules_parse(const char* zJson,
                       uint32_t debug,
                       CACHE_RULES*** pppRules,
                       int32_t* pnRules)
{
    bool rv = false;

    *pppRules = NULL;
    *pnRules = 0;

    json_error_t error;
    json_t* pRoot = json_loads(zJson, JSON_DISABLE_EOF_CHECK, &error);

    if (pRoot)
    {
        rv = cache_rules_create_from_json(pRoot, debug, pppRules, pnRules);

        if (!rv)
        {
            json_decref(pRoot);
        }
    }
    else
    {
        MXS_ERROR("Parsing rules failed: (%d:%d): %s",
                  error.line,
                  error.column,
                  error.text);
    }

    return rv;
}

void cache_rules_free(CACHE_RULES* rules)
{
    if (rules)
    {
        if (rules->root)
        {
            json_decref(rules->root);
        }

        cache_rule_free(rules->store_rules);
        cache_rule_free(rules->use_rules);
        MXS_FREE(rules);
    }
}

void cache_rules_free_array(CACHE_RULES** ppRules, int32_t nRules)
{
    for (auto i = 0; i < nRules; ++i)
    {
        cache_rules_free(ppRules[i]);
    }

    MXS_FREE(ppRules);
}

void cache_rules_print(const CACHE_RULES* self, DCB* dcb, size_t indent)
{
    if (self->root)
    {
        size_t flags = JSON_PRESERVE_ORDER;
        char* s = json_dumps(self->root, JSON_PRESERVE_ORDER | JSON_INDENT(indent));

        if (s)
        {
            dcb_printf(dcb, "%s\n", s);
            free(s);
        }
    }
    else
    {
        dcb_printf(dcb, "{\n}\n");
    }
}

bool cache_rules_should_store(CACHE_RULES* self, int thread_id, const char* default_db, const GWBUF* query)
{
    bool should_store = false;

    CACHE_RULE* rule = self->store_rules;

    if (rule)
    {
        while (rule && !should_store)
        {
            should_store = cache_rule_matches(rule, thread_id, default_db, query);
            rule = rule->next;
        }
    }
    else
    {
        should_store = true;
    }

    return should_store;
}

bool cache_rules_should_use(CACHE_RULES* self, int thread_id, const MXS_SESSION* session)
{
    bool should_use = false;

    CACHE_RULE* rule = self->use_rules;
    const char* user = session_get_user((MXS_SESSION*)session);
    const char* host = session_get_remote((MXS_SESSION*)session);

    if (!user)
    {
        user = "";
    }

    if (!host)
    {
        host = "";
    }

    if (rule)
    {
        char account[strlen(user) + 1 + strlen(host) + 1];
        sprintf(account, "%s@%s", user, host);

        while (rule && !should_use)
        {
            should_use = cache_rule_matches_user(rule, thread_id, account);
            rule = rule->next;
        }
    }
    else
    {
        should_use = true;
    }

    return should_use;
}


CacheRules::CacheRules(CACHE_RULES* pRules)
    : m_pRules(pRules)
{
}

CacheRules::~CacheRules()
{
    cache_rules_free(m_pRules);
}

// static
std::auto_ptr<CacheRules> CacheRules::create(uint32_t debug)
{
    std::auto_ptr<CacheRules> sThis;

    CACHE_RULES* pRules = cache_rules_create(debug);

    if (pRules)
    {
        sThis = std::auto_ptr<CacheRules>(new(std::nothrow) CacheRules(pRules));
    }

    return sThis;
}

// static
bool CacheRules::parse(const char* zJson, uint32_t debug, std::vector<SCacheRules>* pRules)
{
    bool rv = false;

    pRules->clear();

    CACHE_RULES** ppRules;
    int32_t nRules;

    if (cache_rules_parse(zJson, debug, &ppRules, &nRules))
    {
        rv = create_cache_rules(ppRules, nRules, pRules);
    }

    return rv;
}

// static
bool CacheRules::load(const char* zPath, uint32_t debug, std::vector<SCacheRules>* pRules)
{
    bool rv = false;

    pRules->clear();

    CACHE_RULES** ppRules;
    int32_t nRules;

    if (cache_rules_load(zPath, debug, &ppRules, &nRules))
    {
        rv = create_cache_rules(ppRules, nRules, pRules);
    }

    return rv;
}

// static
bool CacheRules::create_cache_rules(CACHE_RULES** ppRules, int32_t nRules, std::vector<SCacheRules>* pRules)
{
    bool rv = false;

    int j = 0;

    try
    {
        std::vector<SCacheRules> rules;
        rules.reserve(nRules);

        for (int i = 0; i < nRules; ++i)
        {
            j = i;
            CacheRules* pRules = new CacheRules(ppRules[i]);
            j = i + 1;

            rules.push_back(SCacheRules(pRules));
        }

        pRules->swap(rules);
        rv = true;
    }
    catch (const std::exception&)
    {
        // Free all CACHE_RULES objects that were not pushed into 'rules' above.
        for (; j < nRules; ++j)
        {
            cache_rules_free(ppRules[j]);
        }
    }

    MXS_FREE(ppRules);

    return rv;
}

const json_t* CacheRules::json() const
{
    return m_pRules->root;
}

bool CacheRules::should_store(const char* zDefault_db, const GWBUF* pQuery) const
{
    return cache_rules_should_store(m_pRules, get_current_thread_id(), zDefault_db, pQuery);
}

bool CacheRules::should_use(const MXS_SESSION* pSession) const
{
    return cache_rules_should_use(m_pRules, get_current_thread_id(), pSession);
}

/*
 * API end
 */

/**
 * Converts a string to an attribute
 *
 * @param           Name/value mapping.
 * @param s         A string
 * @param attribute On successful return contains the corresponding attribute type.
 *
 * @return True if the string could be converted, false otherwise.
 */
static bool cache_rule_attribute_get(struct cache_attribute_mapping* mapping,
                                     const char* s,
                                     cache_rule_attribute_t* attribute)
{
    mxb_assert(attribute);

    while (mapping->name)
    {
        if (strcmp(s, mapping->name) == 0)
        {
            *attribute = mapping->value;
            return true;
        }
        ++mapping;
    }

    return false;
}

/**
 * Converts a string to an operator
 *
 * @param s A string
 * @param op On successful return contains the corresponding operator.
 *
 * @return True if the string could be converted, false otherwise.
 */
static bool cache_rule_op_get(const char* s, cache_rule_op_t* op)
{
    if (strcmp(s, VALUE_OP_EQ) == 0)
    {
        *op = CACHE_OP_EQ;
        return true;
    }

    if (strcmp(s, VALUE_OP_NEQ) == 0)
    {
        *op = CACHE_OP_NEQ;
        return true;
    }

    if (strcmp(s, VALUE_OP_LIKE) == 0)
    {
        *op = CACHE_OP_LIKE;
        return true;
    }

    if (strcmp(s, VALUE_OP_UNLIKE) == 0)
    {
        *op = CACHE_OP_UNLIKE;
        return true;
    }

    return false;
}

/**
 * Creates a CACHE_RULE object doing regexp matching.
 *
 * @param attribute What attribute this rule applies to.
 * @param op        An operator, CACHE_OP_LIKE or CACHE_OP_UNLIKE.
 * @param value     A regular expression.
 * @param debug     The debug level.
 *
 * @return A new rule object or NULL in case of failure.
 */
static CACHE_RULE* cache_rule_create_regexp(cache_rule_attribute_t attribute,
                                            cache_rule_op_t op,
                                            const char* cvalue,
                                            uint32_t debug)
{
    mxb_assert((op == CACHE_OP_LIKE) || (op == CACHE_OP_UNLIKE));

    CACHE_RULE* rule = NULL;

    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code* code = pcre2_compile((PCRE2_SPTR)cvalue,
                                     PCRE2_ZERO_TERMINATED,
                                     0,
                                     &errcode,
                                     &erroffset,
                                     NULL);

    if (code)
    {
        // We do not care about the result. If JIT is not present, we have
        // complained about it already.
        pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);

        int n_threads = config_threadcount();
        mxb_assert(n_threads > 0);

        pcre2_match_data** datas = alloc_match_datas(n_threads, code);

        if (datas)
        {
            rule = (CACHE_RULE*)MXS_CALLOC(1, sizeof(CACHE_RULE));
            char* value = MXS_STRDUP(cvalue);

            if (rule && value)
            {
                rule->attribute = attribute;
                rule->op = op;
                rule->value = value;
                rule->regexp.code = code;
                rule->regexp.datas = datas;
                rule->debug = debug;
            }
            else
            {
                MXS_FREE(value);
                MXS_FREE(rule);
                free_match_datas(n_threads, datas);
                pcre2_code_free(code);
            }
        }
        else
        {
            MXS_ERROR("PCRE2 match data creation failed. Most likely due to a "
                      "lack of available memory.");
            pcre2_code_free(code);
        }
    }
    else
    {
        PCRE2_UCHAR errbuf[512];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        MXS_ERROR("Regex compilation failed at %d for regex '%s': %s",
                  (int)erroffset,
                  cvalue,
                  errbuf);
    }

    return rule;
}

/**
 * Creates a CACHE_RULE object matching users.
 *
 * @param attribute CACHE_ATTRIBUTE_USER
 * @param op        An operator, CACHE_OP_EQ or CACHE_OP_NEQ.
 * @param cvalue    A string in the MySQL user format.
 * @param debug     The debug level.
 *
 * @return A new rule object or NULL in case of failure.
 */
static CACHE_RULE* cache_rule_create_simple_user(cache_rule_attribute_t attribute,
                                                 cache_rule_op_t op,
                                                 const char* cvalue,
                                                 uint32_t debug)
{
    CACHE_RULE* rule = NULL;

    mxb_assert(attribute == CACHE_ATTRIBUTE_USER);
    mxb_assert((op == CACHE_OP_EQ) || (op == CACHE_OP_NEQ));

    bool error = false;
    size_t len = strlen(cvalue);

    char value[strlen(cvalue) + 1];
    strcpy(value, cvalue);

    char* at = strchr(value, '@');
    char* user = value;
    char* host;
    char any[2];    // sizeof("%")

    if (at)
    {
        *at = 0;
        host = at + 1;
    }
    else
    {
        strcpy(any, "%");
        host = any;
    }

    if (mxs_mysql_trim_quotes(user))
    {
        char pcre_user[2 * len + 1];    // Surely enough

        if (*user == 0)
        {
            strcpy(pcre_user, ".*");
        }
        else
        {
            mxs_mysql_name_to_pcre(pcre_user, user, MXS_PCRE_QUOTE_VERBATIM);
        }

        if (mxs_mysql_trim_quotes(host))
        {
            char pcre_host[2 * len + 1];    // Surely enough

            mxs_mysql_name_kind_t rv = mxs_mysql_name_to_pcre(pcre_host, host, MXS_PCRE_QUOTE_WILDCARD);

            if (rv == MXS_MYSQL_NAME_WITH_WILDCARD)
            {
                op = (op == CACHE_OP_EQ ? CACHE_OP_LIKE : CACHE_OP_UNLIKE);

                char regexp[strlen(pcre_user) + 1 + strlen(pcre_host) + 1];

                sprintf(regexp, "%s@%s", pcre_user, pcre_host);

                rule = cache_rule_create_regexp(attribute, op, regexp, debug);
            }
            else
            {
                // No wildcard, no need to use regexp.

                rule = (CACHE_RULE*)MXS_CALLOC(1, sizeof(CACHE_RULE));
                char* value = (char*)MXS_MALLOC(strlen(user) + 1 + strlen(host) + 1);

                if (rule && value)
                {
                    sprintf(value, "%s@%s", user, host);

                    rule->attribute = attribute;
                    rule->op = op;
                    rule->debug = debug;
                    rule->value = value;
                }
                else
                {
                    MXS_FREE(rule);
                    MXS_FREE(value);
                    rule = NULL;
                }
            }
        }
        else
        {
            MXS_ERROR("Could not trim quotes from host %s.", cvalue);
        }
    }
    else
    {
        MXS_ERROR("Could not trim quotes from user %s.", cvalue);
    }

    return rule;
}

/**
 * Creates a CACHE_RULE object matching column/table/database.
 *
 * @param attribute CACHE_ATTRIBUTE_[COLUMN|TABLE|DATABASE]
 * @param op        An operator, CACHE_OP_EQ or CACHE_OP_NEQ.
 * @param cvalue    A name, with 0, 1 or 2 dots.
 * @param debug     The debug level.
 *
 * @return A new rule object or NULL in case of failure.
 */
static CACHE_RULE* cache_rule_create_simple_ctd(cache_rule_attribute_t attribute,
                                                cache_rule_op_t op,
                                                const char* cvalue,
                                                uint32_t debug)
{
    mxb_assert((attribute == CACHE_ATTRIBUTE_COLUMN)
               || (attribute == CACHE_ATTRIBUTE_TABLE)
               || (attribute == CACHE_ATTRIBUTE_DATABASE));
    mxb_assert((op == CACHE_OP_EQ) || (op == CACHE_OP_NEQ));

    CACHE_RULE* rule = (CACHE_RULE*)MXS_CALLOC(1, sizeof(CACHE_RULE));
    char* value = MXS_STRDUP(cvalue);

    if (rule && value)
    {
        rule->attribute = attribute;
        rule->op = op;
        rule->value = value;
        rule->debug = debug;

        bool allocation_failed = false;

        char buffer[strlen(value) + 1];
        strcpy(buffer, value);

        const char* first = NULL;
        const char* second = NULL;
        const char* third = NULL;
        char* dot1 = strchr(buffer, '.');
        char* dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;

        if (dot1 && dot2)
        {
            first = buffer;
            *dot1 = 0;
            second = dot1 + 1;
            *dot2 = 0;
            third = dot2 + 1;
        }
        else if (dot1)
        {
            first = buffer;
            *dot1 = 0;
            second = dot1 + 1;
        }
        else
        {
            first = buffer;
        }

        switch (attribute)
        {
        case CACHE_ATTRIBUTE_COLUMN:
            {
                if (third)      // implies also 'first' and 'second'
                {
                    rule->simple.column = MXS_STRDUP(third);
                    rule->simple.table = MXS_STRDUP(second);
                    rule->simple.database = MXS_STRDUP(first);

                    if (!rule->simple.column || !rule->simple.table || !rule->simple.database)
                    {
                        allocation_failed = true;
                    }
                }
                else if (second)    // implies also 'first'
                {
                    rule->simple.column = MXS_STRDUP(second);
                    rule->simple.table = MXS_STRDUP(first);

                    if (!rule->simple.column || !rule->simple.table)
                    {
                        allocation_failed = true;
                    }
                }
                else    // only 'first'
                {
                    rule->simple.column = MXS_STRDUP(first);

                    if (!rule->simple.column)
                    {
                        allocation_failed = true;
                    }
                }
            }
            break;

        case CACHE_ATTRIBUTE_TABLE:
            if (third)
            {
                MXS_ERROR("A cache rule value for matching a table name, cannot contain two dots: '%s'",
                          cvalue);
                allocation_failed = true;
            }
            else
            {
                if (second)     // implies also 'first'
                {
                    rule->simple.database = MXS_STRDUP(first);
                    rule->simple.table = MXS_STRDUP(second);
                    if (!rule->simple.database || !rule->simple.table)
                    {
                        allocation_failed = true;
                    }
                }
                else    // only 'first'
                {
                    rule->simple.table = MXS_STRDUP(first);
                    if (!rule->simple.table)
                    {
                        allocation_failed = true;
                    }
                }
            }
            break;

        case CACHE_ATTRIBUTE_DATABASE:
            if (second)
            {
                MXS_ERROR("A cache rule value for matching a database, cannot contain a dot: '%s'",
                          cvalue);
                allocation_failed = true;
            }
            else
            {
                rule->simple.database = MXS_STRDUP(first);
                if (!rule->simple.database)
                {
                    allocation_failed = true;
                }
            }
            break;

        default:
            mxb_assert(!true);
        }

        if (allocation_failed)
        {
            MXS_FREE(rule->simple.column);
            MXS_FREE(rule->simple.table);
            MXS_FREE(rule->simple.database);
            MXS_FREE(value);
            MXS_FREE(rule);
            rule = NULL;
        }
    }
    else
    {
        MXS_FREE(value);
        MXS_FREE(rule);
        rule = NULL;
    }

    return rule;
}

/**
 * Creates a CACHE_RULE object matching an entire query.
 *
 * @param attribute CACHE_ATTRIBUTE_QUERY.
 * @param op        An operator, CACHE_OP_EQ or CACHE_OP_NEQ.
 * @param cvalue    A string.
 * @param debug     The debug level.
 *
 * @return A new rule object or NULL in case of failure.
 */
static CACHE_RULE* cache_rule_create_simple_query(cache_rule_attribute_t attribute,
                                                  cache_rule_op_t op,
                                                  const char* cvalue,
                                                  uint32_t debug)
{
    mxb_assert(attribute == CACHE_ATTRIBUTE_QUERY);
    mxb_assert((op == CACHE_OP_EQ) || (op == CACHE_OP_NEQ));

    CACHE_RULE* rule = (CACHE_RULE*)MXS_CALLOC(1, sizeof(CACHE_RULE));
    char* value = MXS_STRDUP(cvalue);

    if (rule && value)
    {
        rule->attribute = attribute;
        rule->op = op;
        rule->debug = debug;
        rule->value = value;
    }
    else
    {
        MXS_FREE(value);
        MXS_FREE(rule);
        rule = NULL;
    }

    return rule;
}

/**
 * Creates a CACHE_RULE object doing simple matching.
 *
 * @param attribute What attribute this rule applies to.
 * @param op        An operator, CACHE_OP_EQ or CACHE_OP_NEQ.
 * @param value     A string.
 * @param debug     The debug level.
 *
 * @return A new rule object or NULL in case of failure.
 */
static CACHE_RULE* cache_rule_create_simple(cache_rule_attribute_t attribute,
                                            cache_rule_op_t op,
                                            const char* cvalue,
                                            uint32_t debug)
{
    mxb_assert((op == CACHE_OP_EQ) || (op == CACHE_OP_NEQ));

    CACHE_RULE* rule = NULL;

    switch (attribute)
    {
    case CACHE_ATTRIBUTE_COLUMN:
    case CACHE_ATTRIBUTE_TABLE:
    case CACHE_ATTRIBUTE_DATABASE:
        rule = cache_rule_create_simple_ctd(attribute, op, cvalue, debug);
        break;

    case CACHE_ATTRIBUTE_USER:
        rule = cache_rule_create_simple_user(attribute, op, cvalue, debug);
        break;

    case CACHE_ATTRIBUTE_QUERY:
        rule = cache_rule_create_simple_query(attribute, op, cvalue, debug);
        break;

    default:
        MXS_ERROR("Unknown attribute type: %d", (int)attribute);
        mxb_assert(!true);
    }

    return rule;
}

/**
 * Creates a CACHE_RULE object.
 *
 * @param attribute What attribute this rule applies to.
 * @param op        What operator is used.
 * @param value     The value.
 * @param debug     The debug level.
 *
 * @param rule The rule to be freed.
 */
static CACHE_RULE* cache_rule_create(cache_rule_attribute_t attribute,
                                     cache_rule_op_t op,
                                     const char* value,
                                     uint32_t debug)
{
    CACHE_RULE* rule = NULL;

    switch (op)
    {
    case CACHE_OP_EQ:
    case CACHE_OP_NEQ:
        rule = cache_rule_create_simple(attribute, op, value, debug);
        break;

    case CACHE_OP_LIKE:
    case CACHE_OP_UNLIKE:
        rule = cache_rule_create_regexp(attribute, op, value, debug);
        break;

    default:
        mxb_assert(!true);
        MXS_ERROR("Internal error.");
        break;
    }

    return rule;
}

/**
 * Frees a CACHE_RULE object (and the one it points to).
 *
 * @param rule The rule to be freed.
 */
static void cache_rule_free(CACHE_RULE* rule)
{
    if (rule)
    {
        if (rule->next)
        {
            cache_rule_free(rule->next);
        }

        MXS_FREE(rule->value);

        if ((rule->op == CACHE_OP_EQ) || (rule->op == CACHE_OP_NEQ))
        {
            MXS_FREE(rule->simple.column);
            MXS_FREE(rule->simple.table);
            MXS_FREE(rule->simple.database);
        }
        else if ((rule->op == CACHE_OP_LIKE) || (rule->op == CACHE_OP_UNLIKE))
        {
            free_match_datas(config_threadcount(), rule->regexp.datas);
            pcre2_code_free(rule->regexp.code);
        }

        MXS_FREE(rule);
    }
}

/**
 * Check whether a value matches a rule.
 *
 * @param self       The rule object.
 * @param thread_id  The thread id of the calling thread.
 * @param value      The value to check.
 *
 * @return True if the value matches, false otherwise.
 */
static bool cache_rule_compare(CACHE_RULE* self, int thread_id, const char* value)
{
    bool rv;

    if (value)
    {
        rv = cache_rule_compare_n(self, thread_id, value, strlen(value));
    }
    else
    {
        if ((self->op == CACHE_OP_EQ) || (self->op == CACHE_OP_LIKE))
        {
            rv = false;
        }
        else
        {
            rv = true;
        }
    }

    return rv;
}

/**
 * Check whether a value matches a rule.
 *
 * @param self       The rule object.
 * @param thread_id  The thread id of the calling thread.
 * @param value      The value to check.
 * @param len        The length of value.
 *
 * @return True if the value matches, false otherwise.
 */
static bool cache_rule_compare_n(CACHE_RULE* self, int thread_id, const char* value, size_t length)
{
    bool compares = false;

    switch (self->op)
    {
    case CACHE_OP_EQ:
    case CACHE_OP_NEQ:
        compares = (strncmp(self->value, value, length) == 0);
        break;

    case CACHE_OP_LIKE:
    case CACHE_OP_UNLIKE:
        mxb_assert((thread_id >= 0) && (thread_id < config_threadcount()));
        compares = (pcre2_match(self->regexp.code,
                                (PCRE2_SPTR)value,
                                length,
                                0,
                                0,
                                self->regexp.datas[thread_id],
                                NULL) >= 0);
        break;

    default:
        mxb_assert(!true);
    }

    if ((self->op == CACHE_OP_NEQ) || (self->op == CACHE_OP_UNLIKE))
    {
        compares = !compares;
    }

    return compares;
}

/**
 * Returns boolean indicating whether the column rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param thread_id  The thread id of current thread.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_column_regexp(CACHE_RULE* self,
                                             int thread_id,
                                             const char* default_db,
                                             const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_COLUMN);
    mxb_assert((self->op == CACHE_OP_LIKE) || (self->op == CACHE_OP_UNLIKE));

    const char* default_database = NULL;

    int n_databases;
    char** databases = qc_get_database_names((GWBUF*)query, &n_databases);

    if (n_databases == 0)
    {
        // If no databases have been mentioned, then we can assume that all
        // tables and columns that are not explcitly qualified refer to the
        // default database.
        default_database = default_db;
    }
    else if ((default_db == NULL) && (n_databases == 1))
    {
        // If there is no default database and exactly one database has been
        // explicitly mentioned, then we can assume all tables and columns that
        // are not explicitly qualified refer to that database.
        default_database = databases[0];
    }

    size_t default_database_len = default_database ? strlen(default_database) : 0;

    int n_tables;
    char** tables = qc_get_table_names((GWBUF*)query, &n_tables, false);

    const char* default_table = NULL;

    if (n_tables == 1)
    {
        // Only if we have exactly one table can we assume anything
        // about a table that has not been mentioned explicitly.
        default_table = tables[0];
    }

    size_t default_table_len = default_table ? strlen(default_table) : 0;

    const QC_FIELD_INFO* infos;
    size_t n_infos;

    qc_get_field_info((GWBUF*)query, &infos, &n_infos);

    bool matches = false;

    size_t i = 0;
    while (!matches && (i < n_infos))
    {
        const QC_FIELD_INFO* info = (infos + i);

        size_t database_len;
        const char* database;

        if (info->database)
        {
            database = info->database;
            database_len = strlen(info->database);
        }
        else
        {
            database = default_database;
            database_len = default_database_len;
        }

        size_t table_len;
        const char* table;

        if (info->table)
        {
            table = info->table;
            table_len = strlen(info->table);
        }
        else
        {
            table = default_table;
            table_len = default_table_len;
        }

        char buffer[database_len + 1 + table_len + 1 + strlen(info->column) + 1];
        buffer[0] = 0;

        if (database)
        {
            strcat(buffer, database);
            strcat(buffer, ".");
        }

        if (table)
        {
            strcat(buffer, table);
            strcat(buffer, ".");
        }

        strcat(buffer, info->column);

        matches = cache_rule_compare(self, thread_id, buffer);

        ++i;
    }

    if (tables)
    {
        for (i = 0; i < (size_t)n_tables; ++i)
        {
            MXS_FREE(tables[i]);
        }
        MXS_FREE(tables);
    }

    if (databases)
    {
        for (i = 0; i < (size_t)n_databases; ++i)
        {
            MXS_FREE(databases[i]);
        }
        MXS_FREE(databases);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the column rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_column_simple(CACHE_RULE* self, const char* default_db, const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_COLUMN);
    mxb_assert((self->op == CACHE_OP_EQ) || (self->op == CACHE_OP_NEQ));

    const char* rule_column = self->simple.column;
    const char* rule_table = self->simple.table;
    const char* rule_database = self->simple.database;

    const char* default_database = NULL;

    int n_databases;
    char** databases = qc_get_database_names((GWBUF*)query, &n_databases);

    if (n_databases == 0)
    {
        // If no databases have been mentioned, then we can assume that all
        // tables and columns that are not explcitly qualified refer to the
        // default database.
        default_database = default_db;
    }
    else if ((default_db == NULL) && (n_databases == 1))
    {
        // If there is no default database and exactly one database has been
        // explicitly mentioned, then we can assume all tables and columns that
        // are not explicitly qualified refer to that database.
        default_database = databases[0];
    }

    int n_tables;
    char** tables = qc_get_table_names((GWBUF*)query, &n_tables, false);

    const char* default_table = NULL;

    if (n_tables == 1)
    {
        // Only if we have exactly one table can we assume anything
        // about a table that has not been mentioned explicitly.
        default_table = tables[0];
    }

    const QC_FIELD_INFO* infos;
    size_t n_infos;

    qc_get_field_info((GWBUF*)query, &infos, &n_infos);

    bool matches = false;

    size_t i = 0;
    while (!matches && (i < n_infos))
    {
        const QC_FIELD_INFO* info = (infos + i);

        if ((strcasecmp(info->column, rule_column) == 0) || strcmp(rule_column, "*") == 0)
        {
            if (rule_table)
            {
                const char* check_table = info->table ? info->table : default_table;

                if (check_table)
                {
                    if (strcasecmp(check_table, rule_table) == 0)
                    {
                        if (rule_database)
                        {
                            const char* check_database =
                                info->database ? info->database : default_database;

                            if (check_database)
                            {
                                if (strcasecmp(check_database, rule_database) == 0)
                                {
                                    // The column, table and database matched.
                                    matches = true;
                                }
                                else
                                {
                                    // The column, table matched but the database did not.
                                    matches = false;
                                }
                            }
                            else
                            {
                                // If the rules specify a database but we do not know the database,
                                // we consider the databases not to match.
                                matches = false;
                            }
                        }
                        else
                        {
                            // If the rule specifies no database, then if the column and the table
                            // matches, the rule matches.
                            matches = true;
                        }
                    }
                    else
                    {
                        // The column matched, but the table did not.
                        matches = false;
                    }
                }
                else
                {
                    // If the rules specify a table but we do not know the table, we
                    // consider the tables not to match.
                    matches = false;
                }
            }
            else
            {
                // The column matched and there is no table rule.
                matches = true;
            }
        }
        else
        {
            // The column did not match.
            matches = false;
        }

        if (self->op == CACHE_OP_NEQ)
        {
            matches = !matches;
        }

        ++i;
    }

    if (tables)
    {
        for (i = 0; i < (size_t)n_tables; ++i)
        {
            MXS_FREE(tables[i]);
        }
        MXS_FREE(tables);
    }

    if (databases)
    {
        for (i = 0; i < (size_t)n_databases; ++i)
        {
            MXS_FREE(databases[i]);
        }
        MXS_FREE(databases);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the column rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param thread_id  The thread id of current thread.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_column(CACHE_RULE* self,
                                      int thread_id,
                                      const char* default_db,
                                      const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_COLUMN);

    bool matches = false;

    switch (self->op)
    {
    case CACHE_OP_EQ:
    case CACHE_OP_NEQ:
        matches = cache_rule_matches_column_simple(self, default_db, query);
        break;

    case CACHE_OP_LIKE:
    case CACHE_OP_UNLIKE:
        matches = cache_rule_matches_column_regexp(self, thread_id, default_db, query);
        break;

    default:
        mxb_assert(!true);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the database rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param thread_id  The thread id of current thread.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_database(CACHE_RULE* self,
                                        int thread_id,
                                        const char* default_db,
                                        const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_DATABASE);

    bool matches = false;

    bool fullnames = true;
    int n;
    char** names = qc_get_table_names((GWBUF*)query, &n, fullnames);    // TODO: Make qc const-correct.

    if (names)
    {
        int i = 0;

        while (!matches && (i < n))
        {
            char* name = names[i];
            char* dot = strchr(name, '.');
            const char* database = NULL;

            if (dot)
            {
                *dot = 0;
                database = name;
            }
            else
            {
                database = default_db;
            }

            matches = cache_rule_compare(self, thread_id, database);

            MXS_FREE(name);
            ++i;
        }

        while (i < n)
        {
            MXS_FREE(names[i++]);
        }

        MXS_FREE(names);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the query rule matches the query or not.
 *
 * @param self        The CACHE_RULE object.
 * @param thread_id   The thread id of the calling thread.
 * @param default_db  The current default db.
 * @param query       The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_query(CACHE_RULE* self,
                                     int thread_id,
                                     const char* default_db,
                                     const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_QUERY);

    char* sql;
    int len;

    // Will succeed, query contains a contiguous COM_QUERY.
    modutil_extract_SQL((GWBUF*)query, &sql, &len);

    return cache_rule_compare_n(self, thread_id, sql, len);
}

/**
 * Returns boolean indicating whether the table regexp rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param thread_id  The thread id of current thread.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_table_regexp(CACHE_RULE* self,
                                            int thread_id,
                                            const char* default_db,
                                            const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_TABLE);
    mxb_assert((self->op == CACHE_OP_LIKE) || (self->op == CACHE_OP_UNLIKE));

    bool matches = false;

    int n;
    char** names = NULL;
    bool fullnames;

    fullnames = true;
    names = qc_get_table_names((GWBUF*)query, &n, fullnames);

    if (names)
    {
        size_t default_db_len = default_db ? strlen(default_db) : 0;

        int i = 0;
        while (!matches && (i < n))
        {
            char* name = names[i];
            char* dot = strchr(name, '.');

            if (!dot)
            {
                // Only "tbl"

                if (default_db)
                {
                    char buffer[default_db_len + 1 + strlen(name) + 1];

                    strcpy(name, default_db);
                    strcpy(name + default_db_len, ".");
                    strcpy(name + default_db_len + 1, name);

                    matches = cache_rule_compare(self, thread_id, name);
                }
                else
                {
                    matches = cache_rule_compare(self, thread_id, name);
                }

                MXS_FREE(names[i]);
            }
            else
            {
                // A qualified name "db.tbl".
                matches = cache_rule_compare(self, thread_id, name);
            }

            ++i;
        }

        if (i < n)
        {
            MXS_FREE(names[i]);
            ++i;
        }

        MXS_FREE(names);
    }
    else if (self->op == CACHE_OP_UNLIKE)
    {
        matches = true;
    }

    return matches;
}

/**
 * Returns boolean indicating whether the table simple rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_table_simple(CACHE_RULE* self, const char* default_db, const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_TABLE);
    mxb_assert((self->op == CACHE_OP_EQ) || (self->op == CACHE_OP_NEQ));

    bool matches = false;

    bool fullnames = false;

    if (self->simple.database)
    {
        fullnames = true;
    }

    int n;
    char** names = NULL;

    names = qc_get_table_names((GWBUF*)query, &n, fullnames);

    if (names)
    {
        int i = 0;
        while (!matches && (i < n))
        {
            char* name = names[i];
            const char* database = NULL;
            const char* table = NULL;

            if (fullnames)
            {
                char* dot = strchr(name, '.');

                if (dot)
                {
                    *dot = 0;

                    database = name;
                    table = dot + 1;
                }
                else
                {
                    database = default_db;
                    table = name;
                }

                if (database)
                {
                    matches =
                        (strcasecmp(self->simple.database, database) == 0)
                        && (strcasecmp(self->simple.table, table) == 0);
                }
            }
            else
            {
                table = name;

                matches = (strcasecmp(self->simple.table, table) == 0);
            }

            if (self->op == CACHE_OP_NEQ)
            {
                matches = !matches;
            }

            MXS_FREE(name);
            ++i;
        }

        if (i < n)
        {
            MXS_FREE(names[i]);
            ++i;
        }

        MXS_FREE(names);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the table rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param thread_id  The thread id of current thread.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_table(CACHE_RULE* self,
                                     int thread_id,
                                     const char* default_db,
                                     const GWBUF* query)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_TABLE);

    bool matches = false;

    switch (self->op)
    {
    case CACHE_OP_EQ:
    case CACHE_OP_NEQ:
        matches = cache_rule_matches_table_simple(self, default_db, query);
        break;

    case CACHE_OP_LIKE:
    case CACHE_OP_UNLIKE:
        matches = cache_rule_matches_table_regexp(self, thread_id, default_db, query);
        break;

    default:
        mxb_assert(!true);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the user rule matches the account or not.
 *
 * @param self       The CACHE_RULE object.
 * @param thread_id  The thread id of current thread.
 * @param account    The account.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_user(CACHE_RULE* self, int thread_id, const char* account)
{
    mxb_assert(self->attribute == CACHE_ATTRIBUTE_USER);

    bool matches = cache_rule_compare(self, thread_id, account);

    if ((matches && (self->debug & CACHE_DEBUG_MATCHING))
        || (!matches && (self->debug & CACHE_DEBUG_NON_MATCHING)))
    {
        const char* text;
        if (matches)
        {
            text = "MATCHES";
        }
        else
        {
            text = "does NOT match";
        }

        MXS_NOTICE("Rule { \"attribute\": \"%s\", \"op\": \"%s\", \"value\": \"%s\" } %s \"%s\".",
                   cache_rule_attribute_to_string(self->attribute),
                   cache_rule_op_to_string(self->op),
                   self->value,
                   text,
                   account);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param thread_id  The thread id of the calling thread.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches(CACHE_RULE* self, int thread_id, const char* default_db, const GWBUF* query)
{
    bool matches = false;

    switch (self->attribute)
    {
    case CACHE_ATTRIBUTE_COLUMN:
        matches = cache_rule_matches_column(self, thread_id, default_db, query);
        break;

    case CACHE_ATTRIBUTE_DATABASE:
        matches = cache_rule_matches_database(self, thread_id, default_db, query);
        break;

    case CACHE_ATTRIBUTE_TABLE:
        matches = cache_rule_matches_table(self, thread_id, default_db, query);
        break;

    case CACHE_ATTRIBUTE_QUERY:
        matches = cache_rule_matches_query(self, thread_id, default_db, query);
        break;

    case CACHE_ATTRIBUTE_USER:
        mxb_assert(!true);
        break;

    default:
        mxb_assert(!true);
    }

    if ((matches && (self->debug & CACHE_DEBUG_MATCHING))
        || (!matches && (self->debug & CACHE_DEBUG_NON_MATCHING)))
    {
        char* sql;
        int sql_len;
        modutil_extract_SQL((GWBUF*)query, &sql, &sql_len);
        const char* text;

        if (matches)
        {
            text = "MATCHES";
        }
        else
        {
            text = "does NOT match";
        }

        MXS_NOTICE("Rule { \"attribute\": \"%s\", \"op\": \"%s\", \"value\": \"%s\" } %s \"%.*s\".",
                   cache_rule_attribute_to_string(self->attribute),
                   cache_rule_op_to_string(self->op),
                   self->value,
                   text,
                   sql_len,
                   sql);
    }

    return matches;
}

/**
 * Append a rule to the tail of a chain or rules.
 *
 * @param head The head of the chain, can be NULL.
 * @param tail The tail to be added to the chain.
 *
 * @return The head.
 */
static CACHE_RULE* cache_rule_append(CACHE_RULE* head, CACHE_RULE* tail)
{
    mxb_assert(tail);

    if (!head)
    {
        head = tail;
    }
    else
    {
        CACHE_RULE* rule = head;

        while (rule->next)
        {
            rule = rule->next;
        }

        rule->next = tail;
    }

    return head;
}

/**
 * Adds a "store" rule to the rules object
 *
 * @param self Pointer to the CACHE_RULES object that is being built.
 * @param rule The rule to be added.
 */
static void cache_rules_add_store_rule(CACHE_RULES* self, CACHE_RULE* rule)
{
    self->store_rules = cache_rule_append(self->store_rules, rule);
}

/**
 * Adds a "store" rule to the rules object
 *
 * @param self Pointer to the CACHE_RULES object that is being built.
 * @param rule The rule to be added.
 */
static void cache_rules_add_use_rule(CACHE_RULES* self, CACHE_RULE* rule)
{
    self->use_rules = cache_rule_append(self->use_rules, rule);
}

/**
 * Creates a rules object from a JSON object.
 *
 * @param root  The root JSON rule object.
 * @param debug The debug level.
 *
 * @return A rules object if the json object could be parsed, NULL otherwise.
 */
static CACHE_RULES* cache_rules_create_from_json(json_t* root, uint32_t debug)
{
    mxb_assert(root);

    CACHE_RULES* rules = cache_rules_create(debug);

    if (rules)
    {
        if (cache_rules_parse_json(rules, root))
        {
            rules->root = root;
        }
        else
        {
            cache_rules_free(rules);
            rules = NULL;
        }
    }

    return rules;
}

/**
 * Parses the caching rules from a json object and returns corresponding object(s).
 *
 * @param pRoot    The root JSON object in the rules file.
 * @param debug    The debug level.
 * @param pppRules [out] Pointer to array of pointers to CACHE_RULES objects.
 * @param pnRules  [out] Pointer to number of items in *ppRules.
 *
 * @note The caller must free the array @c *ppRules and each rules
 *       object in the array.
 *
 * @return bool True, if the rules could be parsed, false otherwise.
 */
static bool cache_rules_create_from_json(json_t* pRoot,
                                         uint32_t debug,
                                         CACHE_RULES*** pppRules,
                                         int32_t* pnRules)
{
    bool rv = false;

    *pppRules = NULL;
    *pnRules = 0;

    if (json_is_array(pRoot))
    {
        int32_t nRules = json_array_size(pRoot);

        CACHE_RULES** ppRules = (CACHE_RULES**)MXS_MALLOC(nRules * sizeof(CACHE_RULES*));

        if (ppRules)
        {
            int i;
            for (i = 0; i < nRules; ++i)
            {
                json_t* pObject = json_array_get(pRoot, i);
                mxb_assert(pObject);

                CACHE_RULES* pRules = cache_rules_create_from_json(pObject, debug);

                if (pRules)
                {
                    ppRules[i] = pRules;
                    // The array element reference was borrowed, so now that we
                    // know a rule could be created, we must increase the reference
                    // count. Otherwise bad things will happen when the reference of
                    // the root object is decreased.
                    json_incref(pObject);
                }
                else
                {
                    break;
                }
            }

            if (i == nRules)
            {
                *pppRules = ppRules;
                *pnRules = nRules;

                // We only store the objects in the array, so now we must get rid
                // of the array so that it does not leak.
                json_decref(pRoot);

                rv = true;
            }
            else
            {
                // Ok, so something went astray.
                for (int j = 0; j < i; ++j)
                {
                    cache_rules_free(ppRules[j]);
                }

                MXS_FREE(ppRules);
            }
        }
    }
    else
    {
        CACHE_RULES** ppRules = (CACHE_RULES**)MXS_MALLOC(1 * sizeof(CACHE_RULES*));

        if (ppRules)
        {
            CACHE_RULES* pRules = cache_rules_create_from_json(pRoot, debug);

            if (pRules)
            {
                ppRules[0] = pRules;

                *pppRules = ppRules;
                *pnRules = 1;

                rv = true;
            }
            else
            {
                MXS_FREE(ppRules);
            }
        }
    }

    return rv;
}

/**
 * Parses the JSON object used for configuring the rules.
 *
 * @param self  Pointer to the CACHE_RULES object that is being built.
 * @param root  The root JSON object in the rules file.
 *
 * @return True, if the object could be parsed, false otherwise.
 */
static bool cache_rules_parse_json(CACHE_RULES* self, json_t* root)
{
    bool parsed = false;
    json_t* store = json_object_get(root, KEY_STORE);

    if (store)
    {
        if (json_is_array(store))
        {
            parsed = cache_rules_parse_array(self, store, KEY_STORE, cache_rules_parse_store_element);
        }
        else
        {
            MXS_ERROR("The cache rules object contains a `%s` key, but it is not an array.", KEY_STORE);
        }
    }

    if (!store || parsed)
    {
        json_t* use = json_object_get(root, KEY_USE);

        if (use)
        {
            if (json_is_array(use))
            {
                parsed = cache_rules_parse_array(self, use, KEY_USE, cache_rules_parse_use_element);
            }
            else
            {
                MXS_ERROR("The cache rules object contains a `%s` key, but it is not an array.", KEY_USE);
            }
        }
        else
        {
            parsed = true;
        }
    }

    return parsed;
}

/**
 * Parses a array.
 *
 * @param self          Pointer to the CACHE_RULES object that is being built.
 * @param array         An array.
 * @param name          The name of the array.
 * @param parse_element Function for parsing an element.
 *
 * @return True, if the array could be parsed, false otherwise.
 */
static bool cache_rules_parse_array(CACHE_RULES* self,
                                    json_t* store,
                                    const char* name,
                                    cache_rules_parse_element_t parse_element)
{
    mxb_assert(json_is_array(store));

    bool parsed = true;

    size_t n = json_array_size(store);
    size_t i = 0;

    while (parsed && (i < n))
    {
        json_t* element = json_array_get(store, i);
        mxb_assert(element);

        if (json_is_object(element))
        {
            parsed = parse_element(self, element, i);
        }
        else
        {
            MXS_ERROR("Element %lu of the '%s' array is not an object.", i, name);
            parsed = false;
        }

        ++i;
    }

    return parsed;
}

/**
 * Parses an object in an array.
 *
 * @param self   Pointer to the CACHE_RULES object that is being built.
 * @param object An object from the "store" array.
 * @param index  Index of the object in the array.
 *
 * @return True, if the object could be parsed, false otherwise.
 */
static CACHE_RULE* cache_rules_parse_element(CACHE_RULES* self,
                                             json_t* object,
                                             const char* array_name,
                                             size_t index,
                                             struct cache_attribute_mapping* mapping)
{
    mxb_assert(json_is_object(object));

    CACHE_RULE* rule = NULL;

    json_t* a = json_object_get(object, KEY_ATTRIBUTE);
    json_t* o = json_object_get(object, KEY_OP);
    json_t* v = json_object_get(object, KEY_VALUE);

    if (a && o && v && json_is_string(a) && json_is_string(o) && json_is_string(v))
    {
        cache_rule_attribute_t attribute;

        if (cache_rule_attribute_get(mapping, json_string_value(a), &attribute))
        {
            cache_rule_op_t op;

            if (cache_rule_op_get(json_string_value(o), &op))
            {
                rule = cache_rule_create(attribute, op, json_string_value(v), self->debug);
            }
            else
            {
                MXS_ERROR("Element %lu in the `%s` array has an invalid value "
                          "\"%s\" for 'op'.",
                          index,
                          array_name,
                          json_string_value(o));
            }
        }
        else
        {
            MXS_ERROR("Element %lu in the `%s` array has an invalid value "
                      "\"%s\" for 'attribute'.",
                      index,
                      array_name,
                      json_string_value(a));
        }
    }
    else
    {
        MXS_ERROR("Element %lu in the `%s` array does not contain "
                  "'attribute', 'op' and/or 'value', or one or all of them "
                  "is not a string.",
                  index,
                  array_name);
    }

    return rule;
}


/**
 * Parses an object in the "store" array.
 *
 * @param self   Pointer to the CACHE_RULES object that is being built.
 * @param object An object from the "store" array.
 * @param index  Index of the object in the array.
 *
 * @return True, if the object could be parsed, false otherwise.
 */
static bool cache_rules_parse_store_element(CACHE_RULES* self, json_t* object, size_t index)
{
    CACHE_RULE* rule = cache_rules_parse_element(self, object, KEY_STORE, index, cache_store_attributes);

    if (rule)
    {
        cache_rules_add_store_rule(self, rule);
    }

    return rule != NULL;
}

/**
 * Parses an object in the "use" array.
 *
 * @param self   Pointer to the CACHE_RULES object that is being built.
 * @param object An object from the "store" array.
 * @param index  Index of the object in the array.
 *
 * @return True, if the object could be parsed, false otherwise.
 */
static bool cache_rules_parse_use_element(CACHE_RULES* self, json_t* object, size_t index)
{
    CACHE_RULE* rule = cache_rules_parse_element(self, object, KEY_USE, index, cache_use_attributes);

    if (rule)
    {
        cache_rules_add_use_rule(self, rule);
    }

    return rule != NULL;
}

/**
 * Allocates array of pcre2 match datas
 *
 * @param count  How many match datas should be allocated.
 * @param code   The pattern to be used.
 *
 * @return Array of specified length, or NULL.
 */
static pcre2_match_data** alloc_match_datas(int count, pcre2_code* code)
{
    pcre2_match_data** datas = (pcre2_match_data**)MXS_CALLOC(count, sizeof(pcre2_match_data*));

    if (datas)
    {
        int i;
        for (i = 0; i < count; ++i)
        {
            datas[i] = pcre2_match_data_create_from_pattern(code, NULL);

            if (!datas[i])
            {
                break;
            }
        }

        if (i != count)
        {
            for (; i >= 0; --i)
            {
                pcre2_match_data_free(datas[i]);
            }

            MXS_FREE(datas);
            datas = NULL;
        }
    }

    return datas;
}

/**
 * Frees array of pcre2 match datas
 *
 * @param count  The length of the array.
 * @param datas  The array of pcre2 match datas.
 */
static void free_match_datas(int count, pcre2_match_data** datas)
{
    for (int i = 0; i < count; ++i)
    {
        pcre2_match_data_free(datas[i]);
    }

    MXS_FREE(datas);
}
