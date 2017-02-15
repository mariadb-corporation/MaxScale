#pragma once
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

#include <maxscale/cppdefs.hh>
#include <memory>
#include <tr1/memory>
#include <string>
#include <vector>
#include <jansson.h>
#include "mysql.hh"

/**
 * @class MaskingRules
 *
 * MaskingRules abstracts the rules of a masking filter.
 */
class MaskingRules
{
    friend class MaskingRulesTester;

public:
    /**
     * @class Rule
     *
     * A Rule represents a single masking rule.
     */
    class Rule
    {
    public:
        /**
         * @class Account
         *
         * An instance of this class is capable of answering the question
         * whether the current user is subject to masking.
         */
        class Account
        {
        public:
            Account();
            virtual ~Account();

            virtual std::string user() const = 0;
            virtual std::string host() const = 0;

            /**
             * Is a user subject to masking?
             *
             * @param zUser  The name of the user.
             * @param zHost  The host of the user.
             *
             * @return True, if the data should be masked.
             */
            virtual bool matches(const char* zUser, const char* zHost) const = 0;
        };

        typedef std::tr1::shared_ptr<Account> SAccount;

        /**
         * Constructor
         *
         * @param column      The column value from the json file.
         * @param table       The table value from the json file.
         * @param database    The database value from the json file.
         * @param value       The value value from the json file.
         * @param fill        The file value from the json file.
         * @param applies_to  Account instances corresponding to the
         *                    accounts listed in 'applies_to' in the json file.
         * @param exempted    Account instances corresponding to the
         *                    accounts listed in 'exempted' in the json file.
         */
        Rule(const std::string& column,
             const std::string& table,
             const std::string& database,
             const std::string& value,
             const std::string& fill,
             const std::vector<SAccount>& applies_to,
             const std::vector<SAccount>& exempted);
        ~Rule();

        std::string match() const;

        const std::string& column() const
        {
            return m_column;
        }
        const std::string& table() const
        {
            return m_table;
        }
        const std::string& database() const
        {
            return m_database;
        }
        const std::string& value() const
        {
            return m_value;
        }
        const std::string& fill() const
        {
            return m_fill;
        }
        const std::vector<SAccount>& applies_to() const
        {
            return m_applies_to;
        }
        const std::vector<SAccount>& exempted() const
        {
            return m_exempted;
        }

        /**
         * Create a Rule instance
         *
         * @param pRule  A json object corresponding to a single
         *               rule in the rules json file.
         *
         * @return A Rule instance or NULL.
         */
        static std::auto_ptr<Rule> create_from(json_t* pRule);

        /**
         * Establish whether a rule matches a column definition and user/host.
         *
         * @param column_def  A column definition.
         * @param zUser       The current user.
         * @param zHost       The current host.
         *
         * @return True, if the rule matches.
         */
        bool matches(const ComQueryResponse::ColumnDef& column_def,
                     const char* zUser,
                     const char* zHost) const;

        void rewrite(LEncString& s) const;

    private:
        Rule(const Rule&);
        Rule& operator = (const Rule&);

    private:
        std::string           m_column;
        std::string           m_table;
        std::string           m_database;
        std::string           m_value;
        std::string           m_fill;
        std::vector<SAccount> m_applies_to;
        std::vector<SAccount> m_exempted;
    };

    ~MaskingRules();

    /**
     * Load rules
     *
     * @param zPath  Path to rules file.
     *
     * @return A rules object, or NULL if the rules could not be loaded.
     *         or parsed.
     */
    static std::auto_ptr<MaskingRules> load(const char* zPath);

    /**
     * Parse rules
     *
     * @param zPath  Path to rules file.
     *
     * @return A rules object, or NULL if the rules could not be parsed.
     */
    static std::auto_ptr<MaskingRules> parse(const char* zJson);

    /**
     * Create rules from JSON object.
     *
     * @param pRoot  Pointer to JSON object.
     *
     * @return A rules object, or NULL if the rules could not be created.
     */
    static std::auto_ptr<MaskingRules> create_from(json_t* pRoot);

    /**
     * Return the rule object that matches a column definition and user/host.
     *
     * @param column_def  A column definition.
     * @param zUser       The current user.
     * @param zHost       The current host.
     *
     * @return A rule object that matches the column definition and user/host
     *         or NULL if no such rule object exists.
     *
     * @attention The returned object remains value only as long as the
     *            @c MaskingRules object remains valid.
     */
    const Rule* get_rule_for(const ComQueryResponse::ColumnDef& column_def,
                             const char* zUser,
                             const char* zHost) const;

    typedef std::tr1::shared_ptr<Rule> SRule;

private:
    MaskingRules(json_t* pRoot, const std::vector<SRule>& rules);

private:
    MaskingRules(const MaskingRules&);
    MaskingRules& operator = (const MaskingRules&);

private:
    json_t*            m_pRoot;
    std::vector<SRule> m_rules;
};
