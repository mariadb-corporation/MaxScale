/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "rules.h"
#include <errno.h>
#include <stdio.h>
#include <maxscale/alloc.h>
#include <modutil.h>
#include <mysql_client_server_protocol.h>
#include <query_classifier.h>
#include <session.h>
#include "cache.h"

static const char KEY_ATTRIBUTE[] = "attribute";
static const char KEY_OP[]        = "op";
static const char KEY_STORE[]     = "store";
static const char KEY_USE[]       = "use";
static const char KEY_VALUE[]     = "value";

static const char VALUE_ATTRIBUTE_COLUMN[]   = "column";
static const char VALUE_ATTRIBUTE_DATABASE[] = "database";
static const char VALUE_ATTRIBUTE_QUERY[]    = "query";
static const char VALUE_ATTRIBUTE_TABLE[]    = "table";
static const char VALUE_ATTRIBUTE_USER[]     = "user";

static const char VALUE_OP_EQ[]     = "=";
static const char VALUE_OP_NEQ[]    = "!=";
static const char VALUE_OP_LIKE[]   = "like";
static const char VALUE_OP_UNLIKE[] = "unlike";

struct cache_attribute_mapping
{
    const char* name;
    int         value;
};

static struct cache_attribute_mapping cache_store_attributes[] =
{
    { VALUE_ATTRIBUTE_COLUMN,   CACHE_ATTRIBUTE_COLUMN },
    { VALUE_ATTRIBUTE_DATABASE, CACHE_ATTRIBUTE_DATABASE },
    { VALUE_ATTRIBUTE_QUERY,    CACHE_ATTRIBUTE_QUERY },
    { VALUE_ATTRIBUTE_TABLE,    CACHE_ATTRIBUTE_TABLE },
    { NULL,                     0 }
};

static struct cache_attribute_mapping cache_use_attributes[] =
{
    { VALUE_ATTRIBUTE_USER, CACHE_ATTRIBUTE_USER },
    { NULL,                 0 }
};

static bool cache_rule_attribute_get(struct cache_attribute_mapping *mapping,
                                     const char *s,
                                     cache_rule_attribute_t *attribute);
static const char *cache_rule_attribute_to_string(cache_rule_attribute_t attribute);

static bool cache_rule_op_get(const char *s, cache_rule_op_t *op);
static const char *cache_rule_op_to_string(cache_rule_op_t op);

static bool cache_rule_compare(CACHE_RULE *rule, const char *value);
static bool cache_rule_compare_n(CACHE_RULE *rule, const char *value, size_t length);
static CACHE_RULE *cache_rule_create_regexp(cache_rule_attribute_t attribute,
                                            cache_rule_op_t        op,
                                            const char            *value,
                                            uint32_t               debug);
static CACHE_RULE *cache_rule_create_simple(cache_rule_attribute_t attribute,
                                            cache_rule_op_t        op,
                                            const char            *value,
                                            uint32_t               debug);
static CACHE_RULE *cache_rule_create(cache_rule_attribute_t attribute,
                                     cache_rule_op_t        op,
                                     const char            *value,
                                     uint32_t               debug);
static bool cache_rule_matches_column(CACHE_RULE *rule,
                                      const char *default_db,
                                      const GWBUF *query);
static bool cache_rule_matches_database(CACHE_RULE *rule,
                                        const char *default_db,
                                        const GWBUF *query);
static bool cache_rule_matches_query(CACHE_RULE *rule,
                                     const char *default_db,
                                     const GWBUF *query);
static bool cache_rule_matches_table(CACHE_RULE *rule,
                                     const char *default_db,
                                     const GWBUF *query);
static bool cache_rule_matches_user(CACHE_RULE *rule, const char *user);
static bool cache_rule_matches(CACHE_RULE *rule,
                               const char *default_db,
                               const GWBUF *query);

static void cache_rule_free(CACHE_RULE *rule);
static bool cache_rule_matches(CACHE_RULE *rule, const char *default_db, const GWBUF *query);

static void cache_rules_add_store_rule(CACHE_RULES* self, CACHE_RULE* rule);
static void cache_rules_add_use_rule(CACHE_RULES* self, CACHE_RULE* rule);
static bool cache_rules_parse_json(CACHE_RULES* self, json_t* root);

typedef bool (*cache_rules_parse_element_t)(CACHE_RULES *self, json_t *object, size_t index);

static bool cache_rules_parse_array(CACHE_RULES *self, json_t *store, const char* name,
                                    cache_rules_parse_element_t);
static bool cache_rules_parse_store_element(CACHE_RULES *self, json_t *object, size_t index);
static bool cache_rules_parse_use_element(CACHE_RULES *self, json_t *object, size_t index);

/*
 * API begin
 */


/**
 * Create a default cache rules object.
 *
 * @param debug The debug level.
 *
 * @return The rules object or NULL is allocation fails.
 */
CACHE_RULES *cache_rules_create(uint32_t debug)
{
    CACHE_RULES *rules = (CACHE_RULES*)MXS_CALLOC(1, sizeof(CACHE_RULES));

    if (rules)
    {
        rules->debug = debug;
    }

    return rules;
}

/**
 * Loads the caching rules from a file and returns corresponding object.
 *
 * @param path  The path of the file containing the rules.
 * @param debug The debug level.
 *
 * @return The corresponding rules object, or NULL in case of error.
 */
CACHE_RULES *cache_rules_load(const char *path, uint32_t debug)
{
    CACHE_RULES *rules = NULL;

    FILE *fp = fopen(path, "r");

    if (fp)
    {
        json_error_t error;
        json_t *root = json_loadf(fp, JSON_DISABLE_EOF_CHECK, &error);

        if (root)
        {
            rules = cache_rules_create(debug);

            if (rules)
            {
                if (!cache_rules_parse_json(rules, root))
                {
                    cache_rules_free(rules);
                    rules = NULL;
                }
            }

            json_decref(root);
        }
        else
        {
            MXS_ERROR("Loading rules file failed: (%s:%d:%d): %s",
                      path, error.line, error.column, error.text);
        }

        fclose(fp);
    }
    else
    {
        char errbuf[STRERROR_BUFLEN];

        MXS_ERROR("Could not open rules file %s for reading: %s",
                  path, strerror_r(errno, errbuf, sizeof(errbuf)));
    }

    return rules;
}

/**
 * Frees the rules object.
 *
 * @param path The path of the file containing the rules.
 *
 * @return The corresponding rules object, or NULL in case of error.
 */
void cache_rules_free(CACHE_RULES *rules)
{
    if (rules)
    {
        cache_rule_free(rules->store_rules);
        cache_rule_free(rules->use_rules);
        MXS_FREE(rules);
    }
}

/**
 * Returns boolean indicating whether the result of the query should be stored.
 *
 * @param self       The CACHE_RULES object.
 * @param default_db The current default database, NULL if there is none.
 * @param query      The query, expected to contain a COM_QUERY.
 *
 * @return True, if the results should be stored.
 */
bool cache_rules_should_store(CACHE_RULES *self, const char *default_db, const GWBUF* query)
{
    bool should_store = false;

    CACHE_RULE *rule = self->store_rules;

    if (rule)
    {
        while (rule && !should_store)
        {
            should_store = cache_rule_matches(rule, default_db, query);
            rule = rule->next;
        }
    }
    else
    {
        should_store = true;
    }

    return should_store;
}

/**
 * Returns boolean indicating whether the cache should be used, that is consulted.
 *
 * @param self     The CACHE_RULES object.
 * @param session  The current session.
 *
 * @return True, if the cache should be used.
 */
bool cache_rules_should_use(CACHE_RULES *self, const SESSION *session)
{
    bool should_use = false;

    CACHE_RULE *rule = self->use_rules;
    const char *user = session_getUser((SESSION*)session);

    if (rule && user)
    {
        while (rule && !should_use)
        {
            should_use = cache_rule_matches_user(rule, user);
            rule = rule->next;
        }
    }
    else
    {
        should_use = true;
    }

    return should_use;
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
static bool cache_rule_attribute_get(struct cache_attribute_mapping *mapping,
                                     const char *s,
                                     cache_rule_attribute_t *attribute)
{
    ss_dassert(attribute);

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
 * Returns a string representation of a attribute.
 *
 * @param attribute An attribute type.
 *
 * @return Corresponding string, not to be freed.
 */
static const char *cache_rule_attribute_to_string(cache_rule_attribute_t attribute)
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

    default:
        ss_dassert(!true);
        return "<invalid>";
    }
}

/**
 * Converts a string to an operator
 *
 * @param s A string
 * @param op On successful return contains the corresponding operator.
 *
 * @return True if the string could be converted, false otherwise.
 */
static bool cache_rule_op_get(const char *s, cache_rule_op_t *op)
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
 * Returns a string representation of an operator.
 *
 * @param op An operator.
 *
 * @return Corresponding string, not to be freed.
 */
static const char *cache_rule_op_to_string(cache_rule_op_t op)
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
        ss_dassert(!true);
        return "<invalid>";
    }
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
static CACHE_RULE *cache_rule_create_regexp(cache_rule_attribute_t attribute,
                                            cache_rule_op_t        op,
                                            const char            *cvalue,
                                            uint32_t               debug)
{
    ss_dassert((op == CACHE_OP_LIKE) || (op == CACHE_OP_UNLIKE));

    CACHE_RULE *rule = NULL;

    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)cvalue, PCRE2_ZERO_TERMINATED, 0,
                                     &errcode, &erroffset, NULL);

    if (code)
    {
        pcre2_match_data *data = pcre2_match_data_create_from_pattern(code, NULL);

        if (data)
        {
            rule = (CACHE_RULE*)MXS_CALLOC(1, sizeof(CACHE_RULE));
            char* value = MXS_STRDUP(cvalue);

            if (rule && value)
            {
                rule->attribute = attribute;
                rule->op = op;
                rule->value = value;
                rule->regexp.code = code;
                rule->regexp.data = data;
                rule->debug = debug;
            }
            else
            {
                MXS_FREE(value);
                MXS_FREE(rule);
                pcre2_match_data_free(data);
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
                  (int)erroffset, cvalue, errbuf);
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
static CACHE_RULE *cache_rule_create_simple(cache_rule_attribute_t attribute,
                                            cache_rule_op_t        op,
                                            const char            *cvalue,
                                            uint32_t               debug)
{
    ss_dassert((op == CACHE_OP_EQ) || (op == CACHE_OP_NEQ));

    CACHE_RULE *rule = (CACHE_RULE*)MXS_CALLOC(1, sizeof(CACHE_RULE));

    char *value = MXS_STRDUP(cvalue);

    if (rule && value)
    {
        rule->attribute = attribute;
        rule->op = op;
        rule->value = value;
        rule->debug = debug;
    }
    else
    {
        MXS_FREE(value);
        MXS_FREE(rule);
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
static CACHE_RULE *cache_rule_create(cache_rule_attribute_t attribute,
                                     cache_rule_op_t        op,
                                     const char            *value,
                                     uint32_t               debug)
{
    CACHE_RULE *rule = NULL;

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
        ss_dassert(!true);
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

        if ((rule->op == CACHE_OP_LIKE) || (rule->op == CACHE_OP_UNLIKE))
        {
            pcre2_match_data_free(rule->regexp.data);
            pcre2_code_free(rule->regexp.code);
        }

        MXS_FREE(rule);
    }
}

/**
 * Check whether a value matches a rule.
 *
 * @param self  The rule object.
 * @param value The value to check.
 *
 * @return True if the value matches, false otherwise.
 */
static bool cache_rule_compare(CACHE_RULE *self, const char *value)
{
    return cache_rule_compare_n(self, value, strlen(value));
}

/**
 * Check whether a value matches a rule.
 *
 * @param self  The rule object.
 * @param value The value to check.
 * @param len   The length of value.
 *
 * @return True if the value matches, false otherwise.
 */
static bool cache_rule_compare_n(CACHE_RULE *self, const char *value, size_t length)
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
        compares = (pcre2_match(self->regexp.code,
                                (PCRE2_SPTR)value, length,
                                0, 0, self->regexp.data, NULL) >= 0);
        break;

    default:
        ss_dassert(!true);
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
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_column(CACHE_RULE *self, const char *default_db, const GWBUF *query)
{
    ss_dassert(self->attribute == CACHE_ATTRIBUTE_COLUMN);
    ss_info_dassert(!true, "Column matching not implemented yet.");

    return false;
}

/**
 * Returns boolean indicating whether the database rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_database(CACHE_RULE *self, const char *default_db, const GWBUF *query)
{
    ss_dassert(self->attribute == CACHE_ATTRIBUTE_DATABASE);

    bool matches = false;

    int n;
    char **names = qc_get_database_names((GWBUF*)query, &n); // TODO: Make qc const-correct.

    if (names)
    {
        int i = 0;

        while (!matches && (i < n))
        {
            matches = cache_rule_compare(self, names[i]);
            ++i;
        }

        for (int i = 0; i < n; ++i)
        {
            MXS_FREE(names[i]);
        }
        MXS_FREE(names);
    }

    if (!matches && default_db)
    {
        matches = cache_rule_compare(self, default_db);
    }

    return matches;
}

/**
 * Returns boolean indicating whether the query rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_query(CACHE_RULE *self, const char *default_db, const GWBUF *query)
{
    ss_dassert(self->attribute == CACHE_ATTRIBUTE_QUERY);

    char* sql;
    int len;

    // Will succeed, query contains a contiguous COM_QUERY.
    modutil_extract_SQL((GWBUF*)query, &sql, &len);

    return cache_rule_compare_n(self, sql, len);
}

/**
 * Returns boolean indicating whether the table rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_table(CACHE_RULE *self, const char *default_db, const GWBUF *query)
{
    ss_dassert(self->attribute == CACHE_ATTRIBUTE_TABLE);

    bool matches = false;

    int n;
    char **names;
    bool fullnames;

    fullnames = false;
    names = qc_get_table_names((GWBUF*)query, &n, fullnames);

    if (names)
    {
        int i = 0;
        while (!matches && (i < n))
        {
            char *name = names[i];
            matches = cache_rule_compare(self, name);
            MXS_FREE(name);
            ++i;
        }

        if (i < n)
        {
            MXS_FREE(names[i]);
            ++i;
        }

        MXS_FREE(names);

        if (!matches)
        {
            fullnames = true;
            names = qc_get_table_names((GWBUF*)query, &n, fullnames);

            size_t default_db_len = default_db ? strlen(default_db) : 0;
            i = 0;

            while (!matches && (i < n))
            {
                char *name = names[i];
                char *dot = strchr(name, '.');

                if (!dot)
                {
                    if (default_db)
                    {
                        name = (char*)MXS_MALLOC(default_db_len + 1 + strlen(name) + 1);

                        strcpy(name, default_db);
                        strcpy(name + default_db_len, ".");
                        strcpy(name + default_db_len + 1, names[i]);

                        MXS_FREE(names[i]);
                        names[i] = name;
                    }
                }

                matches = cache_rule_compare(self, name);
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
    }

    return matches;
}

/**
 * Returns boolean indicating whether the user rule matches the user or not.
 *
 * @param self   The CACHE_RULE object.
 * @param user   The current default db.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches_user(CACHE_RULE *self, const char *user)
{
    ss_dassert(self->attribute == CACHE_ATTRIBUTE_USER);

    return cache_rule_compare(self, user);
}

/**
 * Returns boolean indicating whether the rule matches the query or not.
 *
 * @param self       The CACHE_RULE object.
 * @param default_db The current default db.
 * @param query      The query.
 *
 * @return True, if the rule matches, false otherwise.
 */
static bool cache_rule_matches(CACHE_RULE *self, const char *default_db, const GWBUF *query)
{
    bool matches = false;

    switch (self->attribute)
    {
    case CACHE_ATTRIBUTE_COLUMN:
        matches = cache_rule_matches_column(self, default_db, query);
        break;

    case CACHE_ATTRIBUTE_DATABASE:
        matches = cache_rule_matches_database(self, default_db, query);
        break;

    case CACHE_ATTRIBUTE_TABLE:
        matches = cache_rule_matches_table(self, default_db, query);
        break;

    case CACHE_ATTRIBUTE_QUERY:
        matches = cache_rule_matches_query(self, default_db, query);
        break;

    case CACHE_ATTRIBUTE_USER:
        ss_dassert(!true);
        break;

    default:
        ss_dassert(!true);
    }

    if ((matches && (self->debug & CACHE_DEBUG_MATCHING)) ||
        (!matches && (self->debug & CACHE_DEBUG_NON_MATCHING)))
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
                   sql_len, sql);
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
    ss_dassert(tail);

    if (!head)
    {
        head = tail;
    }
    else
    {
        CACHE_RULE *rule = head;

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
 * Parses the JSON object used for configuring the rules.
 *
 * @param self  Pointer to the CACHE_RULES object that is being built.
 * @param root  The root JSON object in the rules file.
 *
 * @return True, if the object could be parsed, false otherwise.
 */
static bool cache_rules_parse_json(CACHE_RULES *self, json_t *root)
{
    bool parsed = false;
    json_t *store = json_object_get(root, KEY_STORE);

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
        json_t *use = json_object_get(root, KEY_USE);

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
static bool cache_rules_parse_array(CACHE_RULES *self,
                                    json_t *store,
                                    const char *name,
                                    cache_rules_parse_element_t parse_element)
{
    ss_dassert(json_is_array(store));

    bool parsed = true;

    size_t n = json_array_size(store);
    size_t i = 0;

    while (parsed && (i < n))
    {
        json_t *element = json_array_get(store, i);
        ss_dassert(element);

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
static CACHE_RULE *cache_rules_parse_element(CACHE_RULES *self, json_t *object,
                                             const char* array_name, size_t index,
                                             struct cache_attribute_mapping *mapping)
{
    ss_dassert(json_is_object(object));

    CACHE_RULE *rule = NULL;

    json_t *a = json_object_get(object, KEY_ATTRIBUTE);
    json_t *o = json_object_get(object, KEY_OP);
    json_t *v = json_object_get(object, KEY_VALUE);

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
                          "\"%s\" for 'op'.", index, array_name, json_string_value(o));
            }
        }
        else
        {
            MXS_ERROR("Element %lu in the `%s` array has an invalid value "
                      "\"%s\" for 'attribute'.", index, array_name, json_string_value(a));
        }
    }
    else
    {
        MXS_ERROR("Element %lu in the `%s` array does not contain "
                  "'attribute', 'op' and/or 'value', or one or all of them "
                  "is not a string.", index, array_name);
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
static bool cache_rules_parse_store_element(CACHE_RULES *self, json_t *object, size_t index)
{
    CACHE_RULE *rule = cache_rules_parse_element(self, object, KEY_STORE, index, cache_store_attributes);

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
static bool cache_rules_parse_use_element(CACHE_RULES *self, json_t *object, size_t index)
{
    CACHE_RULE *rule = cache_rules_parse_element(self, object, KEY_USE, index, cache_use_attributes);

    if (rule)
    {
        cache_rules_add_use_rule(self, rule);
    }

    return rule != NULL;
}
