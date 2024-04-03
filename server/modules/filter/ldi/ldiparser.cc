/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "ldi"

#include "ldiparser.hh"

#include <maxbase/assert.hh>
#include <maxscale/boost_spirit_utils.hh>

using namespace boost::spirit;
using x3::lit;
using x3::lexeme;
using x3::ascii::char_;
using x3::ascii::alnum;

using Table = x3::variant<std::tuple<std::string, std::string>, std::string>;

struct LoadData
{
    bool        local {false};
    std::string filename;
    Table       table;
    std::string unparsed_sql;
};

//
// Parser for S3 URLs
//

DECLARE_RULE(s3_prefix, "S3:// or gs:// prefix");
const auto s3_prefix_def = lit("S3://") | lit("gs://");

DECLARE_ATTR_RULE(bucket, "Bucket name", std::string);
const auto bucket_def = +(alnum | char_(".-"));

DECLARE_ATTR_RULE(file, "File name", std::string);
const auto file_def = +(alnum | char_("./-"));

DECLARE_ATTR_RULE(s3_url, "S3 URL", S3URL);
const auto s3_url_def = s3_prefix > bucket > lit("/") > file > x3::eoi;

//
// Parser for LOAD DATA [LOCAL] INFILE commands
//

DECLARE_ATTR_RULE(identifier, "SQL identifier", std::string);
const auto identifier_def = lexeme[lit('`') > +(char_ - '`') > lit('`')] | lexeme[+(alnum | char_("_@$"))];

DECLARE_ATTR_RULE(table_identifier, "Table identifier", Table);
const auto table_identifier_def = (identifier >> lit('.') >> identifier) | identifier;

DECLARE_ATTR_RULE(single_quoted_str, "Single-quoted string", std::string);
const auto single_quoted_str_def = lexeme[lit('\'') > +(char_ - char_('\'')) > lit('\'')];

DECLARE_ATTR_RULE(double_quoted_str, "Double-quoted string", std::string);
const auto double_quoted_str_def = lexeme[lit('"') > +(char_ - char_('"')) > lit('"')];

DECLARE_ATTR_RULE(quoted_str, "Quoted URL string", std::string);
const auto quoted_str_def = single_quoted_str | double_quoted_str;

DECLARE_ATTR_RULE(unparsed_sql, "Unparsed SQL", std::string);
const auto unparsed_sql_def = lexeme[*char_];

DECLARE_ATTR_RULE(maybe_local, "Optional LOCAL keyword", bool);
const auto maybe_local_def = -lexeme[lit("LOCAL") >> x3::attr(true)];

DECLARE_ATTR_RULE(load_data_infile, "LOAD DATA INFILE", LoadData);
const auto load_data_infile_def = lit("LOAD") > lit("DATA") > maybe_local > lit("INFILE")
    > quoted_str > lit("INTO") > lit("TABLE") > table_identifier > unparsed_sql > x3::eoi;

// Boost magic that combines the rules to their definitions
BOOST_SPIRIT_DEFINE(
    identifier,
    table_identifier,
    s3_prefix,
    bucket,
    file,
    s3_url,
    single_quoted_str,
    double_quoted_str,
    quoted_str,
    unparsed_sql,
    maybe_local,
    load_data_infile
    );

BOOST_FUSION_ADAPT_STRUCT(S3URL, bucket, filename);
BOOST_FUSION_ADAPT_STRUCT(LoadData, local, filename, table, unparsed_sql);

std::variant<LoadDataInfile, ParseError> parse_ldi(std::string_view sql)
{
    LoadData res;
    auto start = sql.begin();
    auto end = sql.end();
    std::ostringstream err;

    // The x3::with applies the error handler to the grammar, required to enable error printing
    auto err_handler = x3::error_handler<decltype(start)>(start, end, err);
    auto parser = x3::with<x3::error_handler_tag>(std::ref(err_handler))[x3::no_case[load_data_infile]];

    try
    {
        if (x3::phrase_parse(start, end, parser, x3::ascii::space, res))
        {
            // We need to use a visitor to extract the x3::variant value
            struct TableVisitor : public boost::static_visitor<>
            {
                void operator()(std::string& value)
                {
                    table = value;
                }

                void operator()(std::tuple<std::string, std::string>& value)
                {
                    std::tie(db, table) = value;
                }

                std::string table;
                std::string db;
            } visitor;

            boost::apply_visitor(visitor, res.table);

            LoadDataInfile rval;
            rval.filename = res.filename;
            rval.remaining_sql = res.unparsed_sql;
            rval.db = visitor.db;
            rval.table = visitor.table;
            rval.local = res.local;
            return rval;
        }
    }
    catch (const std::exception& e)
    {
        err << e.what();
    }

    return ParseError{err.str()};
}

std::variant<S3URL, ParseError> parse_s3_url(std::string_view sql)
{
    S3URL res;
    auto start = sql.begin();
    auto end = sql.end();
    std::ostringstream err;

    // The x3::with applies the error handler to the grammar, required to enable error printing
    auto err_handler = x3::error_handler<decltype(start)>(start, end, err);
    auto parser = x3::with<x3::error_handler_tag>(std::ref(err_handler))[x3::no_case[s3_url]];

    try
    {
        if (x3::phrase_parse(start, end, parser, x3::ascii::space, res))
        {
            return res;
        }
    }
    catch (const std::exception& e)
    {
        err << e.what();
    }

    return ParseError{err.str()};
}
