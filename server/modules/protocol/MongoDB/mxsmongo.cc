/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mxsmongo.hh"
#include <sstream>

using namespace std;

namespace mxsmongo
{

string to_string(const bson_t& bson)
{
    stringstream ss;

    ss << "{";

    bson_iter_t it;
    if (bson_iter_init(&it, &bson))
    {
        bool has_next = bson_iter_next(&it);

        while (has_next)
        {
            const char* zKey = bson_iter_key(&it);

            ss << zKey << ": ";

            switch (bson_iter_type(&it))
            {
            case BSON_TYPE_EOD:
                ss << "BSON_TYPE_EOD";
                break;

            case BSON_TYPE_DOUBLE:
                ss << "BSON_TYPE_DOUBLE";
                break;

            case BSON_TYPE_UTF8:
                {
                    uint32_t len;
                    const char* pUtf8 = bson_iter_utf8(&it, &len);
                    ss << "\"";
                    ss.write(pUtf8, len);
                    ss << "\"";
                }
                break;

            case BSON_TYPE_DOCUMENT:
                {
                    uint32_t doc_len;
                    const uint8_t* pDoc_data;

                    bson_iter_document(&it, &doc_len, &pDoc_data);

                    bson_t doc;
                    bson_init_static(&doc, pDoc_data, doc_len);

                    ss << to_string(doc);
                }
                break;

            case BSON_TYPE_ARRAY:
                ss << "BSON_TYPE_ARRAY";
                break;

            case BSON_TYPE_BINARY:
                ss << "BSON_TYPE_BINARY";
                break;

            case BSON_TYPE_UNDEFINED:
                ss << "BSON_TYPE_UNDEFINED";
                break;

            case BSON_TYPE_OID:
                ss << "BSON_TYPE_OID";
                break;

            case BSON_TYPE_BOOL:
                ss << "BSON_TYPE_BOOL";
                break;

            case BSON_TYPE_DATE_TIME:
                ss << "TYPE_DATE_TIME";
                break;

            case BSON_TYPE_NULL:
                ss << "BSON_TYPE_NULL";
                break;

            case BSON_TYPE_REGEX:
                ss << "BSON_TYPE_REGEX";
                break;

            case BSON_TYPE_DBPOINTER:
                ss << "BSON_TYPE_DBPOINTER";
                break;

            case BSON_TYPE_CODE:
                ss << "BSON_TYPE_CODE";
                break;

            case BSON_TYPE_SYMBOL:
                ss << "BSON_TYPE_SYMBOL";
                break;

            case BSON_TYPE_CODEWSCOPE:
                ss << "BSON_TYPE_CODEWSCOPE";
                break;

            case BSON_TYPE_INT32:
                ss << bson_iter_int32(&it);
                break;

            case BSON_TYPE_TIMESTAMP:
                ss << "BSON_TYPE_TIMESTAMP";
                break;

            case BSON_TYPE_INT64:
                ss << "BSON_TYPE_INT64";
                break;

            case BSON_TYPE_MAXKEY:
                ss << "BSON_TYPE_MAXKEY";
                break;

            case BSON_TYPE_MINKEY:
                ss << "BSON_TYPE_MINKEY";
                break;

            default:
                ss << "UNKNOWN";
            }

            has_next = bson_iter_next(&it);

            if (has_next)
            {
                ss << ", ";
            }
        }
    }

    ss << "}";

    return ss.str();
}

const char* mongo::opcode_to_string(int code)
{
    switch (code)
    {
    case MONGOC_OPCODE_REPLY:
        return "MONGOC_OPCODE_REPLY";

    case MONGOC_OPCODE_UPDATE:
        return "MONGOC_OPCODE_UPDATE";

    case MONGOC_OPCODE_INSERT:
        return "MONGOC_OPCODE_INSERT";

    case MONGOC_OPCODE_QUERY:
        return "MONGOC_OPCODE_QUERY";

    case MONGOC_OPCODE_GET_MORE:
        return "OPCODE_GET_MORE";

    case MONGOC_OPCODE_DELETE:
        return "MONGOC_OPCODE_DELETE";

    case MONGOC_OPCODE_KILL_CURSORS:
        return "OPCODE_KILL_CURSORS";

    case MONGOC_OPCODE_COMPRESSED:
        return "MONGOC_OPCODE_COMPRESSED";

    case MONGOC_OPCODE_MSG:
        return "MONGOC_OPCODE_MSG";

    default:
        mxb_assert(!true);
        return "MONGOC_OPCODE_UKNOWN";
    }
}

}
