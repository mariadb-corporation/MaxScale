/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pgutils.hh"

bool pgu::is_truthy(const A_Const& a_const)
{
    bool rv = false;

    if (!a_const.isnull)
    {
        // All types start with a type field, so any member of the union can be
        // used for checking that.
        switch (a_const.val.node.type)
        {
        case T_Integer:
            rv = a_const.val.ival.ival != 0;
            break;

        case T_Float:
            rv = a_const.val.fval.fval != 0;
            break;

        case T_Boolean:
            rv = a_const.val.boolval.boolval;
            break;

        case T_String:
            rv = a_const.val.sval.sval && strlen(a_const.val.sval.sval) != 0;
            break;

        case T_BitString:
            mxb_assert(!true);
            break;

        default:
            mxb_assert(!true);
        }
    }

    return rv;
}
