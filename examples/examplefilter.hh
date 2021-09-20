/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <atomic>
#include <maxscale/filter.hh>
#include <maxscale/config2.hh>

#include "examplefiltersession.hh"

/**
 * Defines general data for the filter. This object is generated when MaxScale starts and deleted at
 * shutdown. When MaxScale is routing queries, this object may be accessed from multiple threads
 * concurrently. This should be considered if the object contains fields that are unsafe to
 * access/modify concurrently.
 */
class ExampleFilter : public mxs::Filter
{
    // Prevent copy-constructor and assignment operator usage
    ExampleFilter(const ExampleFilter&);
    ExampleFilter& operator=(const ExampleFilter&);

public:
    ~ExampleFilter();

    struct ExampleConfig : public mxs::config::Configuration
    {
        ExampleConfig(const std::string& name);

        bool collect_global_counts {false};     /**< Should sessions manipulate the total counts */
    };

    /**
     * Creates a new filter instance. A separate function from the ctor is used so that NULL can be
     * returned on failure.
     *
     * @param zName The name given to the filter in the configuration file. Can be stored if required for
     * e.g. log messages.
     *
     * @return The object on success, NULL on failure. Failure is typically caused by an invalid
     * configuration parameter.
     */
    static ExampleFilter* create(const char* zName);

    /*
     * Creates a new session for this filter. This is called when a new client connects.
     *
     * @param pSession The generic MaxScale session object.
     * @return The new session, or NULL on failure.
     */
    ExampleFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService) override;

    /*
     * Returns JSON form diagnostic data. This is called when the admin tool MaxCtrl asks for the status
     * of this filter. Run MaxCtrl with "./maxctrl show filters" in the MaxScale binary directory.
     *
     * @return Json object
     */
    json_t* diagnostics() const override;

    /*
     * Get filter capabilities. This is used by protocol code to find out what kind of data the filter
     * expects.
     *
     * @return Capabilities as a bitfield
     */
    uint64_t getCapabilities() const override;

    /**
     * Get filter configuration. Used by the MaxScale core to configure the instance.
     *
     * @return The configuration for this filter instance
     */
    mxs::config::Configuration& getConfiguration() override;

    // Specific to ExampleFilter. Called by a session when it sees a query.
    void query_seen();

    // Specific to ExampleFilter. Called by a session when it sees a reply.
    void reply_seen();

private:
    // Used by the create function
    ExampleFilter(const std::string& name);

    // The fields are specific to ExampleFilter.
    std::atomic<int> m_total_queries {0};   /**< How many queries has this filter seen */
    std::atomic<int> m_total_replies {0};   /**< How many replies has this filter seen */

    // The object that stores the configuration variables
    ExampleConfig m_config;
};
