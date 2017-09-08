#pragma once
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

#include <maxscale/dcb.h>

MXS_BEGIN_DECLS

void dcb_free_all_memory(DCB *dcb);
void dcb_final_close(DCB *dcb);

/**
 * @brief Increase the reference count of the DCB
 *
 * @param dcb  The dcb whose reference count should be increased.
 */
static inline void dcb_inc_ref(DCB* dcb)
{
    // A DCB starts out with a refcount of 1.
    ss_dassert(dcb->poll.refcount >= 1);

    poll_inc_ref(&dcb->poll);
}

/**
 * @brief Decrease the reference count of the DCB. If it reaches 0,
 *        the DCB will be freed.
 *
 * @param dcb  The dcb whose reference count should be decreased.
 *
 * @return True, if the dcb is still usable after the call, otherwise false.
 *
 * @note If the function returns false, the caller cannot use the dcb
 *       for anything.
 */
static inline bool dcb_dec_ref(DCB* dcb)
{
    // A DCB starts out with a refcount of 1.
    ss_dassert(dcb->poll.refcount >= 1);

    bool rv = true;
    if (poll_dec_ref(&dcb->poll) == 1)
    {
        dcb_free_all_memory(dcb);
        rv = false;
    }
    return rv;
}

/**
 * @brief Increase the reference count of the DCB.
 *
 * Convenience function for the situation where a received dcb is stored
 * and the reference count needs to be increased.
 *
 * @param dcb  The dcb whose reference count should be increased.
 *
 * @return The dcb provided as argument.
 */
static inline DCB* dcb_get_ref(DCB* dcb)
{
    dcb_inc_ref(dcb);
    return dcb;
}

MXS_END_DECLS
