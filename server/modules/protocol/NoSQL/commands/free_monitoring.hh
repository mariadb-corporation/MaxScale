/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/manual/reference/command/nav-free-monitoring/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/getFreeMonitoringStatus/
class GetFreeMonitoringStatus;

template<>
struct IsAdmin<command::GetFreeMonitoringStatus>
{
    static const bool is_admin { true };
};

class GetFreeMonitoringStatus final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "getFreeMonitoringStatus";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    bool is_admin() const override
    {
        return IsAdmin<GetFreeMonitoringStatus>::is_admin;
    }

    void populate_response(DocumentBuilder& doc) override
    {
        doc.append(kvp(key::STATE, value::UNDECIDED));
        doc.append(kvp(key::OK, 1));
    }
};


// https://docs.mongodb.com/manual/reference/command/setFreeMonitoring/


}

}
