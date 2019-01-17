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
#pragma once

#include "clustrixmon.hh"
#include <string>

namespace Clustrix
{

enum class Status
{
    QUORUM,
    STATIC,
    UNKNOWN
};

Status      status_from_string(const std::string& status);
std::string to_string(Status status);

enum class SubState
{
    NORMAL,
    UNKNOWN
};

SubState    substate_from_string(const std::string& substate);
std::string to_string(SubState sub_state);

}
