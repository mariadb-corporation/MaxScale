/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * Internal header for the monitor
 */

#include <maxscale/monitor.hh>

#define MON_ARG_MAX 8192

/* Is not really an event as the other values, but is a valid config setting and also the default.
 * Bitmask value matches all events. */
static const MXS_ENUM_VALUE mxs_monitor_event_default_enum = {"all", ~0ULL};
static const MXS_ENUM_VALUE mxs_monitor_event_enum_values[] =
{
    mxs_monitor_event_default_enum,
    {"master_down",                MASTER_DOWN_EVENT },
    {"master_up",                  MASTER_UP_EVENT   },
    {"slave_down",                 SLAVE_DOWN_EVENT  },
    {"slave_up",                   SLAVE_UP_EVENT    },
    {"server_down",                SERVER_DOWN_EVENT },
    {"server_up",                  SERVER_UP_EVENT   },
    {"synced_down",                SYNCED_DOWN_EVENT },
    {"synced_up",                  SYNCED_UP_EVENT   },
    {"donor_down",                 DONOR_DOWN_EVENT  },
    {"donor_up",                   DONOR_UP_EVENT    },
    {"lost_master",                LOST_MASTER_EVENT },
    {"lost_slave",                 LOST_SLAVE_EVENT  },
    {"lost_synced",                LOST_SYNCED_EVENT },
    {"lost_donor",                 LOST_DONOR_EVENT  },
    {"new_master",                 NEW_MASTER_EVENT  },
    {"new_slave",                  NEW_SLAVE_EVENT   },
    {"new_synced",                 NEW_SYNCED_EVENT  },
    {"new_donor",                  NEW_DONOR_EVENT   },
    {NULL}
};
