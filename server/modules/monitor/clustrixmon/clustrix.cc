/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "clustrix.hh"
#include <maxbase/assert.h>

namespace
{

const char CN_NORMAL[]  = "normal";
const char CN_QUORUM[]  = "quorum";
const char CN_STATIC[]  = "static";
const char CN_UNKNOWN[] = "unknown";

}

std::string Clustrix::to_string(Clustrix::Status status)
{
    switch (status)
    {
    case Status::QUORUM:
        return CN_QUORUM;

    case Status::STATIC:
        return CN_STATIC;

    case Status::UNKNOWN:
        return CN_UNKNOWN;
    }

    mxb_assert(!true);
    return CN_UNKNOWN;
}

Clustrix::Status Clustrix::status_from_string(const std::string& status)
{
    if (status == CN_QUORUM)
    {
        return Status::QUORUM;
    }
    else if (status == CN_STATIC)
    {
        return Status::STATIC;
    }
    else
    {
        MXB_WARNING("'%s' is an unknown status for a Clustrix node.", status.c_str());
        return Status::UNKNOWN;
    }
}

std::string Clustrix::to_string(Clustrix::SubState substate)
{
    switch (substate)
    {
    case SubState::NORMAL:
        return CN_NORMAL;

    case SubState::UNKNOWN:
        return CN_UNKNOWN;
    }

    mxb_assert(!true);
    return CN_UNKNOWN;
}

Clustrix::SubState Clustrix::substate_from_string(const std::string& substate)
{
    if (substate == CN_NORMAL)
    {
        return SubState::NORMAL;
    }
    else
    {
        MXB_WARNING("'%s' is an unknown sub-state for a Clustrix node.", substate.c_str());
        return SubState::UNKNOWN;
    }
}
