/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-03-14
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
    S3URL       s3;
    Table       table;
    std::string unparsed_sql;
};

DECLARE_ATTR_RULE(identifier, "SQL identifier", std::string);
const auto identifier_def = lexeme[lit('`') > +(char_ - '`') > lit('`')] | lexeme[+(alnum | char_("_@$"))];

DECLARE_ATTR_RULE(table_identifier, "Table identifier", Table);
const auto table_identifier_def = (identifier >> lit('.') >> identifier) | identifier;

DECLARE_RULE(s3_prefix, "S3:// or gs:// prefix");
const auto s3_prefix_def = lit("S3://") | lit("gs://");

DECLARE_ATTR_RULE(bucket, "Bucket name", std::string);
const auto bucket_def = +(alnum | char_(".-"));

DECLARE_ATTR_RULE(file, "File name", std::string);
const auto file_def = +(alnum | char_(".-/"));

DECLARE_ATTR_RULE(s3_url, "S3 URL", S3URL);
const auto s3_url_def = s3_prefix > bucket > lit("/") > file;

DECLARE_ATTR_RULE(single_quoted_url, "Single-quoted URL string", S3URL);
const auto single_quoted_url_def = lit('\'') > s3_url > lit('\'');

DECLARE_ATTR_RULE(double_quoted_url, "Double-quoted URL string", S3URL);
const auto double_quoted_url_def = lit('"') > s3_url > lit('"');

DECLARE_ATTR_RULE(quoted_url, "Quoted URL string", S3URL);
const auto quoted_url_def = single_quoted_url | double_quoted_url;

DECLARE_ATTR_RULE(unparsed_sql, "Unparsed SQL", std::string);
const auto unparsed_sql_def = lexeme[*char_];

DECLARE_ATTR_RULE(load_data_infile, "LOAD DATA INFILE", LoadData);
const auto load_data_infile_def = lit("LOAD") > lit("DATA") > lit("INFILE")
    > quoted_url > lit("INTO") > lit("TABLE") > table_identifier > unparsed_sql > x3::eoi;

// Boost magic that combines the rules to their definitions
BOOST_SPIRIT_DEFINE(
    identifier,
    table_identifier,
    s3_prefix,
    bucket,
    file,
    s3_url,
    single_quoted_url,
    double_quoted_url,
    quoted_url,
    unparsed_sql,
    load_data_infile
    );

BOOST_FUSION_ADAPT_STRUCT(S3URL, bucket, filename);
BOOST_FUSION_ADAPT_STRUCT(LoadData, s3, table, unparsed_sql);

std::variant<LoadDataResult, ParseError> parse_ldi(std::string_view sql)
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

            LoadDataResult rval;
            rval.s3 = res.s3;
            rval.remaining_sql = res.unparsed_sql;
            rval.db = visitor.db;
            rval.table = visitor.table;
            return rval;
        }
    }
    catch (const std::exception& e)
    {
        err << e.what();
    }

    return ParseError{err.str()};
}
