/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/target.hh>

const MXS_ENUM_VALUE rank_values[] =
{
    {"primary",   RANK_PRIMARY  },
    {"secondary", RANK_SECONDARY},
    {NULL}
};

const char* DEFAULT_RANK = "primary";

// static
std::string mxs::Target::status_to_string(uint64_t flags, int n_connections)
{
    std::string result;
    std::string separator;

    // Helper function.
    auto concatenate_if = [&result, &separator](bool condition, const std::string& desc) {
            if (condition)
            {
                result += separator + desc;
                separator = ", ";
            }
        };

    // TODO: The following values should be revisited at some point, but since they are printed by
    // the REST API they should not be changed suddenly. Strictly speaking, even the combinations
    // should not change, but this is more dependant on the monitors and have already changed.
    // Also, system tests compare to these strings so the output must stay constant for now.
    const std::string maintenance = "Maintenance";
    const std::string drained = "Drained";
    const std::string draining = "Draining";
    const std::string master = "Master";
    const std::string relay = "Relay Master";
    const std::string slave = "Slave";
    const std::string synced = "Synced";
    const std::string slave_ext = "Slave of External Server";
    const std::string sticky = "Master Stickiness";
    const std::string auth_err = "Auth Error";
    const std::string running = "Running";
    const std::string down = "Down";

    // Maintenance/Draining is usually set by user so is printed first.
    // Draining in the presence of Maintenance has no effect, so we only
    // print either one of those, with Maintenance taking precedence.
    if (status_is_in_maint(flags))
    {
        concatenate_if(true, maintenance);
    }
    else if (status_is_draining(flags))
    {
        if (n_connections == 0)
        {
            concatenate_if(true, drained);
        }
        else
        {
            concatenate_if(true, draining);
        }
    }

    // Master cannot be a relay or a slave.
    if (status_is_master(flags))
    {
        concatenate_if(true, master);
    }
    else
    {
        // Relays are typically slaves as well. The binlog server may be an exception.
        concatenate_if(status_is_relay(flags), relay);
        concatenate_if(status_is_slave(flags), slave);
    }

    // The following Galera and Cluster bits may be combined with master/slave.
    concatenate_if(status_is_joined(flags), synced);
    // May be combined with other MariaDB monitor flags.
    concatenate_if(flags & SERVER_SLAVE_OF_EXT_MASTER, slave_ext);

    // Should this be printed only if server is master?
    concatenate_if(flags & SERVER_MASTER_STICKINESS, sticky);

    concatenate_if(flags & SERVER_AUTH_ERROR, auth_err);
    concatenate_if(status_is_running(flags), running);
    concatenate_if(status_is_down(flags), down);

    return result;
}
