/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <atomic>
#include <maxscale/filter.hh>
#include "examplefiltersession.hh"

/**
 * Defines general data for the filter. This object is generated when MaxScale starts and deleted at
 * shutdown. When MaxScale is routing queries, this object may be accessed from multiple threads
 * concurrently. This should be considered if the object contains fields that are unsafe to
 * access/modify concurrently.
 */
class ExampleFilter : public maxscale::Filter<ExampleFilter, ExampleFilterSession>
{
    // Prevent copy-constructor and assignment operator usage
    ExampleFilter(const ExampleFilter&);
    ExampleFilter& operator=(const ExampleFilter&);

public:
    ~ExampleFilter();

    /**
     * Creates a new filter instance. A separate function from the ctor is used so that NULL can be
     * returned on failure.
     *
     * @param zName The name given to the filter in the configuration file. Can be stored if required for
     * e.g. log messages.
     * @param ppParams Configuration parameters parsed from the configuration file
     * @return The object on success, NULL on failure. Failure is typically caused by an invalid
     * configuration parameter.
     */
    static ExampleFilter* create(const char* zName, MXS_CONFIG_PARAMETER* ppParams);

    /*
     * Creates a new session for this filter. This is called when a new client connects.
     *
     * @param pSession The generic MaxScale session object.
     * @return The new session, or NULL on failure.
     */
    ExampleFilterSession* newSession(MXS_SESSION* pSession);

    /*
     * Print diagnostics to a DCB. This is called when the admin tool MaxAdmin asks for the status of this
     * filter. Run MaxAdmin with "./maxadmin show filters" in the MaxScale binary directory.
     *
     * @param pDcb The connection descriptor to print diagnostic to
     */
    void diagnostics(DCB* pDcb) const;

    /*
     * Returns JSON form diagnostic data. This is called when the admin tool MaxCtrl asks for the status
     * of this filter. Run MaxCtrl with "./maxctrl show filters" in the MaxScale binary directory.
     *
     * @return Json object
     */
    json_t* diagnostics_json() const;

    /*
     * Get filter capabilities. This is used by protocol code to find out what kind of data the filter
     * expects.
     *
     * @return Capabilities as a bitfield
     */
    uint64_t getCapabilities();

    // Specific to ExampleFilter. Called by a session when it sees a query.
    void query_seen();

    // Specific to ExampleFilter. Called by a session when it sees a reply.
    void reply_seen();

private:
    // Used by the create function
    ExampleFilter(const MXS_CONFIG_PARAMETER* pParams);

    // The fields are specific to ExampleFilter.
    std::atomic<int> m_total_queries {0};   /**< How many queries has this filter seen */
    std::atomic<int> m_total_replies {0};   /**< How many replies has this filter seen */

    bool m_collect_global_counts {false};   /**< Should sessions manipulate the total counts */
};
