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
const char CN_CLASSIFICATION[] = "classification";
const char CN_CLASSIFY[] = "classify";
const char CN_FIELDS[] = "fields";
const char CN_FUNCTIONS[] = "functions";
const char CN_HAS_WHERE_CLAUSE[] = "has_where_clause";
const char CN_HITS[] = "hits";
const char CN_OPERATION[] = "operation";
const char CN_PARSE_RESULT[] = "parse_result";
const char CN_TYPE_MASK[] = "type_mask";
const char CN_CANONICAL[] = "canonical";

class ThisUnit
{
public:
    ThisUnit()
        : classifier(nullptr)
    {
    }

    ThisUnit(const ThisUnit&) = delete;
    ThisUnit& operator=(const ThisUnit&) = delete;

    QUERY_CLASSIFIER* classifier;
};

static ThisUnit this_unit;

}


bool qc_setup(const QC_CACHE_PROPERTIES* cache_properties)
{
    QC_TRACE();
    mxb_assert(!this_unit.classifier);

    int64_t cache_max_size = (cache_properties ? cache_properties->max_size : 0);
    mxb_assert(cache_max_size >= 0);

    if (cache_max_size)
    {
        // Config::n_threads as MaxScale is not yet running.
        int64_t size_per_thr = cache_max_size / mxs::Config::get().n_threads;
        MXB_NOTICE("Query classification results are cached and reused. "
                   "Memory used per thread: %s", mxb::pretty_size(size_per_thr).c_str());
    }
    else
    {
        MXB_NOTICE("Query classification results are not cached.");
    }

    QC_CACHE_PROPERTIES properties;
    properties.max_size = cache_max_size;

    return mxs::CachingParser::set_properties(properties);
}

bool qc_thread_init(uint32_t kind)
{
    QC_TRACE();

    bool rc = false;

    if (kind & QC_INIT_SELF)
    {
        mxs::CachingParser::thread_init();
        rc = true;
    }
    else
    {
        rc = true;
    }

    if (rc)
    {
        if (kind & QC_INIT_PLUGIN)
        {
            mxb_assert(this_unit.classifier);
            rc = this_unit.classifier->thread_init() == 0;
        }

        if (!rc)
        {
            if (kind & QC_INIT_SELF)
            {
                mxs::CachingParser::thread_finish();
            }
        }
    }

    return rc;
}

void qc_thread_end(uint32_t kind)
{
    QC_TRACE();

    if (kind & QC_INIT_PLUGIN)
    {
        mxb_assert(this_unit.classifier);
        this_unit.classifier->thread_end();
    }

    if (kind & QC_INIT_SELF)
    {
        mxs::CachingParser::thread_finish();
    }
}

bool qc_get_current_stmt(const char** ppStmt, size_t* pLen)
{
    QC_TRACE();
    // TODO: This will be NULL. Fix this later.
    // mxb_assert(this_unit.classifier);

    *ppStmt = 0;
    *pLen = 0;

    return true;

    //return this_unit.classifier->get_current_stmt(ppStmt, pLen) == QC_RESULT_OK;
}

std::unique_ptr<json_t> qc_as_json(const char* zHost)
{
    QC_CACHE_PROPERTIES properties;
    mxs::CachingParser::get_properties(&properties);

    json_t* pParams = json_object();
    json_object_set_new(pParams, CN_CACHE_SIZE, json_integer(properties.max_size));

    json_t* pAttributes = json_object();
    json_object_set_new(pAttributes, CN_PARAMETERS, pParams);

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(CN_QUERY_CLASSIFIER));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_QUERY_CLASSIFIER));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC, pSelf));
}

namespace
{

json_t* get_params(json_t* pJson)
{
    json_t* pParams = mxb::json_ptr(pJson, MXS_JSON_PTR_PARAMETERS);

    if (pParams && json_is_object(pParams))
    {
        if (auto pSize = mxb::json_ptr(pParams, CN_CACHE_SIZE))
        {
            if (!json_is_null(pSize) && !json_is_integer(pSize))
            {
                pParams = nullptr;
            }
        }
    }

    return pParams;
}
}

bool qc_alter_from_json(json_t* pJson)
{
    bool rv = false;

    json_t* pParams = get_params(pJson);

    if (pParams)
    {
        rv = true;

        QC_CACHE_PROPERTIES cache_properties;
        mxs::CachingParser::get_properties(&cache_properties);

        json_t* pValue;

        if ((pValue = mxb::json_ptr(pParams, CN_CACHE_SIZE)))
        {
            cache_properties.max_size = json_integer_value(pValue);
            // If get_params() did its job, then we will not
            // get here if the value is negative.
            mxb_assert(cache_properties.max_size >= 0);
        }

        if (rv)
        {
            MXB_AT_DEBUG(bool set = ) mxs::CachingParser::set_properties(cache_properties);
            mxb_assert(set);
        }
    }

    return rv;
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

namespace
{

json_t* cache_entry_as_json(const std::string& stmt, const QC_CACHE_ENTRY& entry)
{
    json_t* pHits = json_integer(entry.hits);

    json_t* pClassification = json_object();
    json_object_set_new(pClassification,
                        CN_PARSE_RESULT, json_string(mxs::parser::to_string(entry.result.status)));
    std::string type_mask = mxs::Parser::type_mask_to_string(entry.result.type_mask);
    json_object_set_new(pClassification, CN_TYPE_MASK, json_string(type_mask.c_str()));
    json_object_set_new(pClassification,
                        CN_OPERATION,
                        json_string(mxs::Parser::op_to_string(entry.result.op)));

    json_t* pAttributes = json_object();
    json_object_set_new(pAttributes, CN_HITS, pHits);
    json_object_set_new(pAttributes, CN_CLASSIFICATION, pClassification);

    json_t* pSelf = json_object();
    json_object_set_new(pSelf, CN_ID, json_string(stmt.c_str()));
    json_object_set_new(pSelf, CN_TYPE, json_string(CN_CACHE));
    json_object_set_new(pSelf, CN_ATTRIBUTES, pAttributes);

    return pSelf;
}
}

std::unique_ptr<json_t> qc_cache_as_json(const char* zHost)
{
    std::map<std::string, QC_CACHE_ENTRY> state;

    // Assuming the classification cache of all workers will roughly be similar
    // (which will be the case unless something is broken), collecting the
    // information serially from all routing workers will consume 1/N of the
    // memory that would be consumed if the information were collected in
    // parallel and then coalesced here.

    mxs::RoutingWorker::execute_serially([&state]() {
                                             mxs::CachingParser::get_thread_cache_state(state);
                                         });

    json_t* pData = json_array();

    for (const auto& p : state)
    {
        const auto& stmt = p.first;
        const auto& entry = p.second;

        json_t* pEntry = cache_entry_as_json(stmt, entry);

        json_array_append_new(pData, pEntry);
    }

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_CACHE, pData));
}
