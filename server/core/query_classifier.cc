/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/query_classifier.hh>

#include <inttypes.h>
#include <algorithm>
#include <atomic>
#include <random>
#include <unordered_map>
#include <maxbase/alloc.hh>
#include <maxbase/format.hh>
#include <maxbase/pretty_print.hh>
#include <maxscale/buffer.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/json_api.hh>
#include <maxscale/modutil.hh>
#include <maxscale/parser.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/routingworker.hh>
#include <maxsimd/canonical.hh>

// #define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined (QC_TRACE_ENABLED)
#define QC_TRACE() MXB_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

namespace
{

const char DEFAULT_QC_NAME[] = "qc_sqlite";
const char CN_ARGUMENTS[] = "arguments";
const char CN_CACHE[] = "cache";
const char CN_CACHE_SIZE[] = "cache_size";
const char CN_CLASSIFY[] = "classify";
const char CN_FIELDS[] = "fields";
const char CN_FUNCTIONS[] = "functions";
const char CN_HAS_WHERE_CLAUSE[] = "has_where_clause";
const char CN_OPERATION[] = "operation";
const char CN_PARSE_RESULT[] = "parse_result";
const char CN_TYPE_MASK[] = "type_mask";
const char CN_CANONICAL[] = "canonical";

}

namespace
{

void append_field_info(json_t* pParent,
                       const char* zName,
                       const QC_FIELD_INFO* begin, const QC_FIELD_INFO* end)
{
    json_t* pFields = json_array();

    std::for_each(begin, end, [pFields](const QC_FIELD_INFO& info) {
                      std::string name;

                      if (!info.database.empty())
                      {
                          name += info.database;
                          name += '.';
                          mxb_assert(!info.table.empty());
                      }

                      if (!info.table.empty())
                      {
                          name += info.table;
                          name += '.';
                      }

                      mxb_assert(!info.column.empty());

                      name += info.column;

                      json_array_append_new(pFields, json_string(name.c_str()));
                  });

    json_object_set_new(pParent, zName, pFields);
}

void append_field_info(mxs::Parser& parser, json_t* pParams, GWBUF* pBuffer)
{
    const QC_FIELD_INFO* begin;
    size_t n;
    parser.get_field_info(pBuffer, &begin, &n);

    append_field_info(pParams, CN_FIELDS, begin, begin + n);
}

void append_function_info(mxs::Parser& parser, json_t* pParams, GWBUF* pBuffer)
{
    json_t* pFunctions = json_array();

    const QC_FUNCTION_INFO* begin;
    size_t n;
    parser.get_function_info(pBuffer, &begin, &n);

    std::for_each(begin, begin + n, [&parser, pFunctions](const QC_FUNCTION_INFO& info) {
                      json_t* pFunction = json_object();

                      json_object_set_new(pFunction, CN_NAME,
                                          json_stringn(info.name.data(), info.name.length()));

                      append_field_info(pFunction, CN_ARGUMENTS, info.fields, info.fields + info.n_fields);

                      json_array_append_new(pFunctions, pFunction);
                  });

    json_object_set_new(pParams, CN_FUNCTIONS, pFunctions);
}
}

std::unique_ptr<json_t> qc_classify_as_json(const char* zHost, const std::string& statement)
{
    mxs::Parser& parser = MariaDBParser::get();

    json_t* pAttributes = json_object();

    GWBUF buffer = mariadb::create_query(statement);
    GWBUF* pBuffer = &buffer;

    qc_parse_result_t result = parser.parse(pBuffer, QC_COLLECT_ALL);

    json_object_set_new(pAttributes, CN_PARSE_RESULT, json_string(mxs::parser::to_string(result)));

    if (result != QC_QUERY_INVALID)
    {
        std::string type_mask = mxs::Parser::type_mask_to_string(parser.get_type_mask(pBuffer));
        json_object_set_new(pAttributes, CN_TYPE_MASK, json_string(type_mask.c_str()));

        json_object_set_new(pAttributes, CN_OPERATION,
                            json_string(mxs::Parser::op_to_string(parser.get_operation(pBuffer))));

        append_field_info(parser, pAttributes, pBuffer);
        append_function_info(parser, pAttributes, pBuffer);

        json_object_set_new(pAttributes, CN_CANONICAL, json_string(pBuffer->get_canonical().c_str()));
    }

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_CLASSIFY));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_CLASSIFY, pSelf));
}

