/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

// Something in the Spirit X3 headers has a shadowing declaration that causes an error
// if used with -Wshadow=local
#pragma GCC diagnostic ignored "-Wshadow=local"

//
// Reusable Spirit X3 utilities used in MaxScale. The file also includes all the necessary headers to use it
// with most of the standard types (e.g. tuples).
//

#define BOOST_ERROR_CODE_HEADER_ONLY 1

// This prevents automatic rule name deduction, helps keep the error messages cleaner.
#define BOOST_SPIRIT_X3_NO_RTTI

#include <boost/fusion/adapted/struct/adapt_struct.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/std_tuple.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/support/ast/position_tagged.hpp>
#include <boost/spirit/home/x3/support/ast/variant.hpp>
#include <boost/spirit/home/x3/support/utility/annotate_on_success.hpp>
#include <boost/spirit/home/x3/support/utility/error_reporting.hpp>

namespace maxscale
{
// Error handler that the rule types must inherit from, allows pretty-printing of errors
struct error_handler
{
    template<typename Iterator, typename Exception, typename Context>
    boost::spirit::x3::error_handler_result on_error(Iterator& first, Iterator const& last,
                                                     Exception const& x, Context const& context)
    {
        auto& error_handler = boost::spirit::x3::get<boost::spirit::x3::error_handler_tag>(context).get();
        std::string message;

        if (x.which() == "undefined")
        {
            message = "Syntax error.";
        }
        else
        {
            message = "Error! Expecting `" + x.which() + "`:";
        }

        error_handler(x.where(), message);
        return boost::spirit::x3::error_handler_result::fail;
    }
};
}

/**
 * Declare a rule with an attribute
 *
 * @param id        Rule ID, declared as a variable
 * @param desc      Rule type description
 * @param attr_type Rule attribute (i.e. return value)
 */
#define DECLARE_ATTR_RULE(id, desc, attr_type) \
        struct id : public maxscale::error_handler {}; \
        const boost::spirit::x3::rule<struct id, attr_type> id = desc

/**
 * Declare a rule
 *
 * The rule attribute is deduced using the rule definition.
 *
 * @param id        Rule ID, declared as a variable
 * @param desc      Rule type description
 */
#define DECLARE_RULE(id, desc) \
        struct id : public maxscale::error_handler {}; \
        const boost::spirit::x3::rule<struct id> id = desc

#pragma GCC diagnostic pop
