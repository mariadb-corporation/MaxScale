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

#include "rules.hh"

#include <maxscale/buffer.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>

Rule::Rule(std::string name):
    data(NULL),
    name(name),
    type(RT_PERMISSION),
    on_queries(FW_OP_UNDEFINED),
    times_matched(0),
    active(NULL)
{
}

Rule::~Rule()
{
}

bool Rule::matches_query(GWBUF* buffer, char** msg)
{
    *msg = create_error("Permission denied at this time.");
    MXS_NOTICE("rule '%s': query denied at this time.", name.c_str());
    return true;
}

bool Rule::need_full_parsing(GWBUF* buffer) const
{
    bool rval = false;

    if (type == RT_COLUMN ||
        type == RT_FUNCTION ||
        type == RT_USES_FUNCTION ||
        type == RT_WILDCARD ||
        type == RT_CLAUSE)
    {
        switch (qc_get_operation(buffer))
        {
        case QUERY_OP_SELECT:
        case QUERY_OP_UPDATE:
        case QUERY_OP_INSERT:
        case QUERY_OP_DELETE:
            rval = true;
            break;

        default:
            break;
        }
    }

    return rval;
}

bool Rule::matches_query_type(GWBUF* buffer)
{
    qc_query_op_t optype = qc_get_operation(buffer);

    return on_queries == FW_OP_UNDEFINED ||
           (on_queries & qc_op_to_fw_op(optype)) ||
           (MYSQL_IS_COM_INIT_DB(GWBUF_DATA(buffer)) &&
            (on_queries & FW_OP_CHANGE_DB));
}
