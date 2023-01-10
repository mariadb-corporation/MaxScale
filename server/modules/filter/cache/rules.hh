#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <stdbool.h>
#include <memory>
#include <vector>
#include <maxbase/jansson.hh>
#include <maxscale/buffer.hh>
#include <maxscale/session.hh>
#include <maxscale/pcre2.hh>

enum cache_rule_attribute_t
{
    CACHE_ATTRIBUTE_COLUMN,
    CACHE_ATTRIBUTE_DATABASE,
    CACHE_ATTRIBUTE_QUERY,
    CACHE_ATTRIBUTE_TABLE,
    CACHE_ATTRIBUTE_USER,
};

enum cache_rule_op_t
{
    CACHE_OP_EQ,
    CACHE_OP_NEQ,
    CACHE_OP_LIKE,
    CACHE_OP_UNLIKE
};


class CacheRule
{
public:
    virtual ~CacheRule();

    virtual cache_rule_attribute_t attribute() const = 0;
    virtual cache_rule_op_t op() const = 0;
    virtual std::string value() const = 0;
    virtual uint32_t debug() const = 0;

    virtual bool compare(const std::string_view& value) const = 0;
    virtual bool compare_n(const char* value, size_t length) const = 0;
};

class CacheRuleConcrete : public CacheRule
{
public:
    cache_rule_attribute_t attribute() const override final
    {
        return m_attribute;
    }

    cache_rule_op_t op() const override final
    {
        return m_op;
    }

    std::string value() const override final
    {
        return m_value;
    }

    uint32_t debug() const override final
    {
        return m_debug;
    }

    bool compare(const std::string_view& value) const override final;

protected:
    CacheRuleConcrete(cache_rule_attribute_t attribute, // What attribute is evalued.
                      cache_rule_op_t op,               // What operator is used.
                      std::string value,                // The value from the rule file.
                      uint32_t debug)                   // Debug bits
        : m_attribute(attribute)
        , m_op(op)
        , m_value(std::move(value))
        , m_debug(debug)
    {
    }

    cache_rule_attribute_t m_attribute;   // What attribute is evalued.
    cache_rule_op_t        m_op;          // What operator is used.
    std::string            m_value;       // The value from the rule file.
    uint32_t               m_debug;       // The debug bits.

};

class CacheRuleValue : public CacheRuleConcrete
{
public:
    bool matches(const char* default_db, const GWBUF* query) const;

protected:
    virtual bool matches_column(const char* default_db, const GWBUF* query) const;
    virtual bool matches_table(const char* default_db, const GWBUF* query) const;
    bool matches_database(const char* default_db, const GWBUF* query) const;
    bool matches_query(const char* default_db, const GWBUF* query) const;

protected:
    CacheRuleValue(cache_rule_attribute_t attribute, // What attribute is evalued.
                   cache_rule_op_t op,               // What operator is used.
                   std::string value,                // The value from the rule file.
                   uint32_t debug)                   // Debug bits
        : CacheRuleConcrete(attribute, op, value, debug)
    {
    }
};

class CacheRuleSimple : public CacheRuleValue
{
public:
    static bool compare_n(const std::string& lhs,
                          cache_rule_op_t op,
                          const char* value, size_t length);

protected:
    CacheRuleSimple(cache_rule_attribute_t attribute, // What attribute is evalued.
                    cache_rule_op_t op,               // What operator is used.
                    std::string value,                // The value from the rule file.
                    uint32_t debug)                   // Debug bits
        : CacheRuleValue(attribute, op, value, debug)
    {
        mxb_assert(op == CACHE_OP_EQ || op == CACHE_OP_NEQ);
    }

    bool compare_n(const char* value, size_t length) const override final;
};

class CacheRuleCTD final : public CacheRuleSimple
{
public:
    static CacheRuleCTD* create(cache_rule_attribute_t attribute, // What attribute is evalued.
                                cache_rule_op_t op,               // What operator is used.
                                const char* zValue,               // The value from the rule file.
                                uint32_t debug);                  // Debug bits

protected:
    bool matches_column(const char* default_db, const GWBUF* query) const override;
    bool matches_table(const char* default_db, const GWBUF* query) const override;

    struct
    {
        std::string column;
        std::string table;
        std::string database;
    } m_ctd;

private:
    CacheRuleCTD(cache_rule_attribute_t attribute, // What attribute is evalued.
                 cache_rule_op_t op,               // What operator is used.
                 std::string value,                // The value from the rule file.
                 uint32_t debug)                   // Debug bits
        : CacheRuleSimple(attribute, op, value, debug)
    {
    }
};

class CacheRuleQuery final : public CacheRuleSimple
{
public:
    static CacheRuleQuery* create(cache_rule_attribute_t attribute, // What attribute is evalued.
                                  cache_rule_op_t op,               // What operator is used.
                                  const char* zValue,               // The value from the rule file.
                                  uint32_t debug);                  // Debug bits

private:
    CacheRuleQuery(cache_rule_attribute_t attribute, // What attribute is evalued.
                   cache_rule_op_t op,               // What operator is used.
                   std::string value,                // The value from the rule file.
                   uint32_t debug)                   // Debug bits
        : CacheRuleSimple(attribute, op, value, debug)
    {
    }
};

class CacheRuleRegex final : public CacheRuleValue
{
public:
    ~CacheRuleRegex();

    bool compare_n(const char* value, size_t length) const override final;

    static CacheRuleRegex* create(cache_rule_attribute_t attribute, // What attribute is evalued.
                                  cache_rule_op_t op,               // What operator is used.
                                  const char* zValue,               // The value from the rule file.
                                  uint32_t debug);                  // Debug bits

protected:
    bool matches_column(const char* default_db, const GWBUF* query) const override;
    bool matches_table(const char* default_db, const GWBUF* query) const override;

private:
    CacheRuleRegex(cache_rule_attribute_t attribute, // What attribute is evalued.
                   cache_rule_op_t op,               // What operator is used.
                   std::string value,                // The value from the rule file.
                   uint32_t debug)                   // Debug bits
        : CacheRuleValue(attribute, op, value, debug)
    {
        mxb_assert(op == CACHE_OP_LIKE || op == CACHE_OP_UNLIKE);
    }

    struct
    {
        pcre2_code* code;
    } m_regexp;
};

class CacheRuleUser final : public CacheRule
{
public:
    static CacheRuleUser* create(cache_rule_attribute_t attribute, // What attribute is evalued.
                                 cache_rule_op_t op,               // What operator is used.
                                 const char* zValue,               // The value from the rule file.
                                 uint32_t debug);                  // Debug bits

    cache_rule_attribute_t attribute() const override
    {
        return m_sDelegate->attribute();
    }

    cache_rule_op_t op() const override
    {
        return m_sDelegate->op();
    }

    std::string value() const override
    {
        return m_sDelegate->value();
    }

    uint32_t debug() const override
    {
        return m_sDelegate->debug();
    }

    bool matches_user(const char* account) const;

    bool compare(const std::string_view& value) const override;
    bool compare_n(const char* value, size_t length) const override;

private:
    CacheRuleUser(std::unique_ptr<CacheRule> sDelegate)
        : m_sDelegate(std::move(sDelegate))
    {
    }

    std::unique_ptr<CacheRule> m_sDelegate;
};

/**
 * Returns a string representation of a attribute.
 *
 * @param attribute An attribute type.
 *
 * @return Corresponding string, not to be freed.
 */
const char* cache_rule_attribute_to_string(cache_rule_attribute_t attribute);

/**
 * Returns a string representation of an operator.
 *
 * @param op An operator.
 *
 * @return Corresponding string, not to be freed.
 */
const char* cache_rule_op_to_string(cache_rule_op_t op);

/**
 * Loads the caching rules from a file and returns corresponding object.
 *
 * @param path     The path of the file containing the rules.
 * @param debug    The debug level.
 * @param pppRules [out] Pointer to array of pointers to CacheRules objects.
 * @param pnRules  [out] Pointer to number of items in @c *ppRules.
 *
 * @note The caller must free the array @c *pppRules and each rules
 *       object in the array.
 *
 * @return bool True, if the rules could be loaded, false otherwise.
 */
class CacheRules;
using SCacheRules = std::shared_ptr<CacheRules>;
bool cache_rules_load(const char* zPath,
                      uint32_t debug,
                      std::vector<SCacheRules>* pRules);

/**
 * Prints the rules.
 *
 * @param pdcb    The DCB where the rules should be printed.
 * @param indent  By how many spaces to indent the output.
 */
void cache_rules_print(const CacheRules* rules, DCB* dcb, size_t indent);

class CacheRules
{
public:
    using SCacheRules = std::shared_ptr<CacheRules>;
    using S = SCacheRules;
    using Vector = std::vector<S>;

    CacheRules(const CacheRules&) = delete;
    CacheRules& operator=(const CacheRules&) = delete;

    ~CacheRules();

    /**
     * Creates an empty rules object.
     *
     * @param debug The debug level.
     *
     * @return An empty rules object, or NULL in case of error.
     */
    static std::unique_ptr<CacheRules> create(uint32_t debug);

    /**
     * Parses the caching rules from a string.
     *
     * @param zJson  Null-terminate string containing JSON.
     * @param debug  The debug level.
     * @param pRules [out] The loaded rules.
     *
     * @return True, if the rules could be parsed, false otherwise.
     */
    static bool parse(const char* zJson, uint32_t debug, std::vector<SCacheRules>* pRules);

    /**
     * Loads the caching rules from a file.
     *
     * @param path   The path of the file containing the rules.
     * @param debug  The debug level.
     * @param pRules [out] The loaded rules.
     *
     * @return True, if the rules could be loaded, false otherwise.
     */
    static bool load(const char* zPath, uint32_t debug, std::vector<SCacheRules>* pRules);
    static bool load(const std::string& path, uint32_t debug, std::vector<SCacheRules>* pRules)
    {
        return load(path.c_str(), debug, pRules);
    }

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

public: // Temporarily
    CacheRules(uint32_t debug);

    static bool create_from_json(json_t* root, uint32_t debug, std::vector<SCacheRules>* pRules);
    static CacheRules* create_from_json(json_t* root, uint32_t debug);

    bool parse_json(json_t* root);

    using SCacheRuleValue = std::unique_ptr<CacheRuleValue>;
    using SCacheRuleUser = std::unique_ptr<CacheRuleUser>;

    using SCacheRuleValueVector = std::vector<SCacheRuleValue>;
    using SCacheRuleUserVector = std::vector<SCacheRuleUser>;

    json_t*               root { nullptr }; // The JSON root object.
    uint32_t              debug { 0 };      // The debug level.
    SCacheRuleValueVector store_rules;      // The rules for when to store data to the cache.
    SCacheRuleUserVector  use_rules;        // The rules for when to use data from the cache.
};
