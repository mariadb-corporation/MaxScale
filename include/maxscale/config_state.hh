/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/json.hh>


namespace maxscale
{
/**
 * Helper class for storing a JSON state and comparing to the stored state later on
 */
class ConfigState
{
public:

    /**
     * The function that returns the logical state of the configuration
     *
     * @return The configuration state as JSON
     */
    virtual mxb::Json config_state() const = 0;

    /**
     * Stores the current state of the configuration
     */
    void store_config_state();

    /**
     * Check if the state of the configuration is the same as it was on startup. This is only true if the
     * object was read from a static configuration file. For objects that were constructed from runtime files,
     * this always returns false.
     *
     * @return True if the configuration is in the same state as it was on startup
     */
    bool in_static_config_state() const;

private:
    mxb::Json get_config_state() const;

    mxb::Json m_stored {mxb::Json::Type::UNDEFINED};
};
}
