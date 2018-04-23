#pragma once
#ifndef _MAXSCALE_FILTER_CACHE_RULES_H
#define _MAXSCALE_FILTER_CACHE_RULES_H
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

#include <maxscale/cdefs.h>
#include <stdbool.h>
#include <jansson.h>
#include <maxscale/buffer.h>
#include <maxscale/session.h>
#include <maxscale/pcre2.h>

MXS_BEGIN_DECLS

typedef enum cache_rule_attribute
{
    CACHE_ATTRIBUTE_COLUMN,
    CACHE_ATTRIBUTE_DATABASE,
    CACHE_ATTRIBUTE_QUERY,
    CACHE_ATTRIBUTE_TABLE,
    CACHE_ATTRIBUTE_USER,
} cache_rule_attribute_t;

typedef enum cache_rule_op
{
    CACHE_OP_EQ,
    CACHE_OP_NEQ,
    CACHE_OP_LIKE,
    CACHE_OP_UNLIKE
} cache_rule_op_t;


typedef struct cache_rule
{
    cache_rule_attribute_t attribute; // What attribute is evalued.
    cache_rule_op_t        op;        // What operator is used.
    char                  *value;     // The value from the rule file.
    struct
    {
        char *database;
        char *table;
        char *column;
    } simple;                         // Details, only for CACHE_OP_[EQ|NEQ]
    struct
    {
        pcre2_code        *code;
        pcre2_match_data **datas;
    } regexp;                         // Regexp data, only for CACHE_OP_[LIKE|UNLIKE].
    uint32_t               debug;     // The debug level.
    struct cache_rule     *next;
} CACHE_RULE;

typedef struct cache_rules
{
    json_t     *root;         // The JSON root object.
    uint32_t    debug;        // The debug level.
    CACHE_RULE *store_rules;  // The rules for when to store data to the cache.
    CACHE_RULE *use_rules;    // The rules for when to use data from the cache.
} CACHE_RULES;

/**
 * Returns a string representation of a attribute.
 *
 * @param attribute An attribute type.
 *
 * @return Corresponding string, not to be freed.
 */
const char *cache_rule_attribute_to_string(cache_rule_attribute_t attribute);

/**
 * Returns a string representation of an operator.
 *
 * @param op An operator.
 *
 * @return Corresponding string, not to be freed.
 */
const char *cache_rule_op_to_string(cache_rule_op_t op);

/**
 * Create a default cache rules object.
 *
 * @param debug The debug level.
 *
 * @return The rules object or NULL is allocation fails.
 */
CACHE_RULES *cache_rules_create(uint32_t debug);

/**
 * Frees the rules object.
 *
 * @param path The path of the file containing the rules.
 *
 * @return The corresponding rules object, or NULL in case of error.
 */
void cache_rules_free(CACHE_RULES *rules);

/**
 * Loads the caching rules from a file and returns corresponding object.
 *
 * @param path     The path of the file containing the rules.
 * @param debug    The debug level.
 * @param pppRules [out] Pointer to array of pointers to CACHE_RULES objects.
 * @param pnRules  [out] Pointer to number of items in @c *ppRules.
 *
 * @note The caller must free the array @c *pppRules and each rules
 *       object in the array.
 *
 * @return bool True, if the rules could be loaded, false otherwise.
 */
bool cache_rules_load(const char* zPath, uint32_t debug,
                      CACHE_RULES*** pppRules, int32_t* pnRules);

/**
 * Parses the caching rules from a string and returns corresponding object.
 *
 * @param json     String containing json.
 * @param debug    The debug level.
 * @param pppRules [out] Pointer to array of pointers to CACHE_RULES objects.
 * @param pnRules  [out] Pointer to number of items in *ppRules.
 *
 * @note The caller must free the array @c *ppRules and each rules
 *       object in the array.
 *
 * @return bool True, if the rules could be parsed, false otherwise.
 */
bool cache_rules_parse(const char *json, uint32_t debug,
                       CACHE_RULES*** pppRules, int32_t* pnRules);

/**
 * Prints the rules.
 *
 * @param pdcb    The DCB where the rules should be printed.
 * @param indent  By how many spaces to indent the output.
 */
void cache_rules_print(const CACHE_RULES *rules, DCB* dcb, size_t indent);

/**
 * Returns boolean indicating whether the result of the query should be stored.
 *
 * @param rules      The CACHE_RULES object.
 * @param thread_id  The thread id of current thread.
 * @param default_db The current default database, NULL if there is none.
 * @param query      The query, expected to contain a COM_QUERY.
 *
 * @return True, if the results should be stored.
 */
bool cache_rules_should_store(CACHE_RULES *rules, int thread_id, const char *default_db, const GWBUF* query);

/**
 * Returns boolean indicating whether the cache should be used, that is consulted.
 *
 * @param rules      The CACHE_RULES object.
 * @param thread_id  The thread id of current thread.
 * @param session    The current session.
 *
 * @return True, if the cache should be used.
 */
bool cache_rules_should_use(CACHE_RULES *rules, int thread_id, const MXS_SESSION *session);

MXS_END_DECLS

#if defined(__cplusplus)

class CacheRules
{
public:
    ~CacheRules();

    /**
     * Creates an empty rules object.
     *
     * @param debug The debug level.
     *
     * @return An empty rules object, or NULL in case of error.
     */
    static CacheRules* create(uint32_t debug);

    /**
     * Loads the caching rules from a file and returns corresponding object.
     *
     * @param path  The path of the file containing the rules.
     * @param debug The debug level.
     *
     * @return The corresponding rules object, or NULL in case of error.
     */
    static CacheRules* load(const char *zPath, uint32_t debug);

    /**
     * Returns the json rules object.
     *
     * NOTE: The object remains valid only as long as the CacheRules
     *       object is valid.
     *
     * @return The rules object.
     */
    const json_t* json() const;

    /**
     * Returns boolean indicating whether the result of the query should be stored.
     *
     * @param zdefault_db The current default database, NULL if there is none.
     * @param pquery      The query, expected to contain a COM_QUERY.
     *
     * @return True, if the results should be stored.
     */
    bool should_store(const char* zDefault_db, const GWBUF* pQuery) const;

    /**
     * Returns boolean indicating whether the cache should be used, that is consulted.
     *
     * @param psession  The current session.
     *
     * @return True, if the cache should be used.
     */
    bool should_use(const MXS_SESSION* pSession) const;

private:
    CacheRules(CACHE_RULES* pRules);

    CacheRules(const CacheRules&);
    CacheRules& operator = (const CacheRules&);

private:
    CACHE_RULES* m_pRules;
};

#endif

#endif
