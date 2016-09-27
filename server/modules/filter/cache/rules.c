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
#include <query_classifier.h>
#include <mysql_client_server_protocol.h>
#include "cache.h"

static const char KEY_ATTRIBUTE[] = "attribute";
static const char KEY_COLUMN[]    = "column";
static const char KEY_OP[]        = "op";
static const char KEY_QUERY[]     = "query";
static const char KEY_STORE[]     = "store";
static const char KEY_TABLE[]     = "table";
static const char KEY_USE[]       = "use";
static const char KEY_VALUE[]     = "value";

static const char VALUE_ATTRIBUTE_COLUMN[]   = "column";
static const char VALUE_ATTRIBUTE_DATABASE[] = "database";
static const char VALUE_ATTRIBUTE_QUERY[]    = "query";
static const char VALUE_ATTRIBUTE_TABLE[]    = "table";

static const char VALUE_OP_EQ[]     = "=";
static const char VALUE_OP_NEQ[]    = "!=";
static const char VALUE_OP_LIKE[]   = "like";
static const char VALUE_OP_UNLIKE[] = "unlike";

static bool cache_rule_attribute_get(const char *s, cache_rule_attribute_t *attribute);
static const char *cache_rule_attribute_to_string(cache_rule_attribute_t attribute);

static bool cache_rule_op_get(const char *s, cache_rule_op_t *op);
static const char *cache_rule_op_to_string(cache_rule_op_t op);

static bool cache_rule_compare(CACHE_RULE *rule, const char *value);
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
static bool cache_rule_compare(CACHE_RULE *rule, const char *value);
static bool cache_rule_matches(CACHE_RULE *rule,
                               const char *default_db,
                               const GWBUF *query);

static void cache_rule_free(CACHE_RULE *rule);
static bool cache_rule_matches(CACHE_RULE *rule, const char *default_db, const GWBUF *query);

static void cache_rules_add_store_rule(CACHE_RULES* self, CACHE_RULE* rule);
static bool cache_rules_parse_json(CACHE_RULES* self, json_t* root);
static bool cache_rules_parse_store(CACHE_RULES *self, json_t *store);
static bool cache_rules_parse_store_element(CACHE_RULES *self, json_t *object, size_t index);

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
    // TODO: Also support user.
    return true;
}

/*
 * API end
 */

/**
 * Converts a string to an attribute
 *
 * @param s         A string
 * @param attribute On successful return contains the corresponding attribute type.
 *
 * @return True if the string could be converted, false otherwise.
 */
static bool cache_rule_attribute_get(const char *s, cache_rule_attribute_t *attribute)
{
    if (strcmp(s, VALUE_ATTRIBUTE_COLUMN) == 0)
    {
        *attribute = CACHE_ATTRIBUTE_COLUMN;
        return true;
    }

    if (strcmp(s, VALUE_ATTRIBUTE_DATABASE) == 0)
    {
        *attribute = CACHE_ATTRIBUTE_DATABASE;
        return true;
    }

    if (strcmp(s, VALUE_ATTRIBUTE_QUERY) == 0)
    {
        *attribute = CACHE_ATTRIBUTE_QUERY;
        return true;
    }

    if (strcmp(s, VALUE_ATTRIBUTE_TABLE) == 0)
    {
        *attribute = CACHE_ATTRIBUTE_TABLE;
        return true;
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
    bool compares = false;

    switch (self->op)
    {
    case CACHE_OP_EQ:
    case CACHE_OP_NEQ:
        compares = (strcmp(self->value, value) == 0);
        break;

    case CACHE_OP_LIKE:
    case CACHE_OP_UNLIKE:
        compares = (pcre2_match(self->regexp.code, (PCRE2_SPTR)value,
                                PCRE2_ZERO_TERMINATED, 0, 0,
                                self->regexp.data, NULL) >= 0);
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
        // TODO: Not implemented yet.
        ss_dassert(!true);
        break;

    case CACHE_ATTRIBUTE_DATABASE:
        {
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
        }
        break;

    case CACHE_ATTRIBUTE_TABLE:
        // TODO: Not implemented yet.
        ss_dassert(!true);
        break;

    case CACHE_ATTRIBUTE_QUERY:
        // TODO: Not implemented yet.
        ss_dassert(!true);
        break;

    default:
        ss_dassert(!true);
    }

    if ((matches && (self->debug & CACHE_DEBUG_MATCHING)) ||
        (!matches && (self->debug & CACHE_DEBUG_NON_MATCHING)))
    {
        const char *sql = GWBUF_DATA(query) + MYSQL_HEADER_LEN + 1; // Header + command byte.
        int sql_len = GWBUF_LENGTH(query) - MYSQL_HEADER_LEN - 1;
        const char* text;

        if (matches)
        {
            text = "MATCHES";
        }
        else
        {
            text = "does NOT match";
        }

        MXS_NOTICE("Rule { \"attribute\": \"%s\", \"op\": \"%s\", \"value\": \"%s\" } %s \"%*s\".",
                   cache_rule_attribute_to_string(self->attribute),
                   cache_rule_op_to_string(self->op),
                   self->value,
                   text,
                   sql_len, sql);
    }

    return matches;
}

/**
 * Adds a "store" rule to the rules object
 *
 * @param self Pointer to the CACHE_RULES object that is being built.
 * @param rule The rule to be added.
 */
static void cache_rules_add_store_rule(CACHE_RULES* self, CACHE_RULE* rule)
{
    if (self->store_rules)
    {
        CACHE_RULE *r = self->store_rules;

        while (r->next)
        {
            r = r->next;
        }

        r->next = rule;
    }
    else
    {
        self->store_rules = rule;
    }
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
            parsed = cache_rules_parse_store(self, store);
        }
        else
        {
            MXS_ERROR("The cache rules object contains a `store` key, but it is not an array.");
        }
    }

    // TODO: Parse 'use' as well.

    return parsed;
}

/**
 * Parses the "store" array.
 *
 * @param self   Pointer to the CACHE_RULES object that is being built.
 * @param store  The "store" array.
 *
 * @return True, if the array could be parsed, false otherwise.
 */
static bool cache_rules_parse_store(CACHE_RULES *self, json_t *store)
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
            parsed = cache_rules_parse_store_element(self, element, i);
        }
        else
        {
            MXS_ERROR("Element %lu of the 'store' array is not an object.", i);
            parsed = false;
        }

        ++i;
    }

    return parsed;
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
    bool parsed = false;
    ss_dassert(json_is_object(object));

    json_t *a = json_object_get(object, KEY_ATTRIBUTE);
    json_t *o = json_object_get(object, KEY_OP);
    json_t *v = json_object_get(object, KEY_VALUE);

    if (a && o && v && json_is_string(a) && json_is_string(o) && json_is_string(v))
    {
        cache_rule_attribute_t attribute;

        if (cache_rule_attribute_get(json_string_value(a), &attribute))
        {
            cache_rule_op_t op;

            if (cache_rule_op_get(json_string_value(o), &op))
            {
                CACHE_RULE *rule = cache_rule_create(attribute, op, json_string_value(v), self->debug);

                if (rule)
                {
                    cache_rules_add_store_rule(self, rule);
                    parsed = true;
                }
            }
            else
            {
                MXS_ERROR("Element %lu in the `store` array has an invalid value "
                          "\"%s\" for 'op'.", index, json_string_value(o));
            }
        }
        else
        {
            MXS_ERROR("Element %lu in the `store` array has an invalid value "
                      "\"%s\" for 'attribute'.", index, json_string_value(a));
        }
    }
    else
    {
        MXS_ERROR("Element %lu in the `store` array does not contain "
                  "'attribute', 'op' and/or 'value', or one or all of them "
                  "is not a string.", index);
    }

    return parsed;
}
