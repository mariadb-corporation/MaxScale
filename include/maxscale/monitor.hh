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

#include <maxscale/cppdefs.hh>
#include <maxscale/monitor.h>
#include <maxscale/semaphore.hh>
#include <maxscale/thread.h>

namespace maxscale
{

class MonitorInstance : public  MXS_MONITOR_INSTANCE
{
public:
    MonitorInstance(const MonitorInstance&) = delete;
    MonitorInstance& operator = (const MonitorInstance&) = delete;

    virtual ~MonitorInstance();

    /**
     * @brief Current state of the monitor.
     *
     * Note that in principle the state of the monitor may already have
     * changed when the current state is returned. The state can be fully
     * trusted only if it is asked in a context when it is known that nobody
     * else can affect it.
     *
     * @return @c MXS_MONITOR_RUNNING if the monitor is running,
     *         @c MXS_MONITOR_STOPPING if the monitor is stopping, and
     *         @c MXS_MONITOR_STOPPED of the monitor is stopped.
     */
    int32_t state() const;

    /**
     * @brief Starts the monitor.
     *
     * - Calls @c has_sufficient_permissions(), if it has not been done earlier.
     * - Updates the 'script' and 'events' configuration paramameters.
     * - Calls @c configure().
     * - Starts the monitor thread.
     *
     * @param param  The parameters of the monitor.
     *
     * @return True, if the monitor started, false otherwise.
     */
    bool start(const MXS_CONFIG_PARAMETER* params);

    /**
     * @brief Stops the monitor.
     *
     * When the function returns, the monitor has stopped.
     */
    void stop();

    /**
     * @brief Write diagnostics
     *
     * The implementation should write diagnostic information to the
     * provided dcb. The default implementation writes nothing.
     *
     * @param dcb  The dcb to write to.
     */
    virtual void diagnostics(DCB* dcb) const;

    /**
     * @brief Obtain diagnostics
     *
     * The implementation should create a JSON object and fill it with diagnostics
     * information. The default implementation returns an object that is populated
     * with the keys 'script' and 'events' if they have been set, otherwise the
     * object is empty.
     *
     * @return An object, if there is information to return, NULL otherwise.
     */
    virtual json_t* diagnostics_json() const;

protected:
    MonitorInstance(MXS_MONITOR* pMonitor);

    const std::string& script() const { return m_script; }
    uint64_t           events() const { return m_events; }

    /**
     * @brief Configure the monitor.
     *
     * When the monitor is started, this function will be called in order
     * to allow the concrete implementation to configure itself from
     * configuration parameters. The default implementation returns true.
     *
     * @return True, if the monitor could be configured, false otherwise.
     *
     * @note If false is returned, then the monitor will not be started.
     */
    virtual bool configure(const MXS_CONFIG_PARAMETER* pParams);

    /**
     * @brief Check whether the monitor has sufficient rights
     *
     * The implementation should check whether the monitor user has sufficient
     * rights to access the servers. The default implementation returns True.
     *
     * @return True, if the monitor user has sufficient rights, false otherwise.
     */
    virtual bool has_sufficient_permissions() const;

    /**
     * @brief Update server information
     *
     * The implementation should probe the server in question and update
     * the server status bits.
     */
    virtual void update_server_status(MXS_MONITORED_SERVER* pMonitored_server) = 0;

    /**
     * @brief Flush pending server status to each server.
     *
     * This function is expected to flush the pending status to each server.
     * The default implementation simply copies monitored_server->pending_status
     * to server->status.
     */
    virtual void flush_server_status();

    /**
     * @brief Monitor the servers
     *
     * This function is called once per monitor round, and the concrete
     * implementation should probe all servers, i.e. call @c update_server_status
     * on each server.
     *
     * The default implementation will for each server:
     *   - Do nothing, if the server is in maintenance.
     *   - Before calling, store the previous status of the server.
     *   - Before calling, set the pending status of the monitored server object
     *     to the status of the corresponding server object.
     *   - Ensure that there is a connection to the server.
     *     If there is, @c update_server_status is called.
     *     If there is not, the pending status will be updated accordingly and
     *     @c update_server_status will *not* be called.
     *   - After the call, update the error count of the server if it is down.
     *
     * Finally, it will call @c flush_server_status.
     */
    virtual void tick();

    MXS_MONITOR*          m_monitor;  /**< The generic monitor structure. */
    MXS_MONITORED_SERVER* m_master;   /**< Master server */

private:
    int32_t     m_state;     /**< The current state of the monitor. */
    THREAD      m_thread;    /**< The thread handle of the monitoring thread. */
    int32_t     m_shutdown;  /**< Non-zero if the monitor should shut down. */
    bool        m_checked;   /**< Whether server access has been checked. */
    std::string m_script;    /**< Launchable script. */
    uint64_t    m_events;    /**< Enabled monitor events. */
    Semaphore   m_semaphore; /**< Semaphore for synchronizing with monitor thread. */

    void main();

    static void main(void* pArg);
};

/**
 * The purpose of the template MonitorApi is to provide an implementation
 * of the monitor C-API. The template is instantiated with a class that
 * provides the actual behaviour of a monitor.
 */
template<class MonitorInstance>
class MonitorApi
{
public:
    MonitorApi() = delete;
    MonitorApi(const MonitorApi&) = delete;
    MonitorApi& operator = (const MonitorApi&) = delete;

    static MXS_MONITOR_INSTANCE* createInstance(MXS_MONITOR* pMonitor)
    {
        MonitorInstance* pInstance = NULL;
        MXS_EXCEPTION_GUARD(pInstance = MonitorInstance::create(pMonitor));
        return pInstance;
    }

    static void destroyInstance(MXS_MONITOR_INSTANCE* pInstance)
    {
        MXS_EXCEPTION_GUARD(delete static_cast<MonitorInstance*>(pInstance));
    }

    static bool startMonitor(MXS_MONITOR_INSTANCE* pInstance,
                             const MXS_CONFIG_PARAMETER* pParams)
    {
        bool started = false;
        MXS_EXCEPTION_GUARD(started = static_cast<MonitorInstance*>(pInstance)->start(pParams));
        return started;
    }

    static void stopMonitor(MXS_MONITOR_INSTANCE* pInstance)
    {
        MXS_EXCEPTION_GUARD(static_cast<MonitorInstance*>(pInstance)->stop());
    }

    static void diagnostics(const MXS_MONITOR_INSTANCE* pInstance, DCB* pDcb)
    {
        MXS_EXCEPTION_GUARD(static_cast<const MonitorInstance*>(pInstance)->diagnostics(pDcb));
    }

    static json_t* diagnostics_json(const MXS_MONITOR_INSTANCE* pInstance)
    {
        json_t* pJson = NULL;
        MXS_EXCEPTION_GUARD(pJson = static_cast<const MonitorInstance*>(pInstance)->diagnostics_json());
        return pJson;
    }

    static MXS_MONITOR_API s_api;
};

template<class MonitorInstance>
MXS_MONITOR_API MonitorApi<MonitorInstance>::s_api =
{
    &MonitorApi<MonitorInstance>::createInstance,
    &MonitorApi<MonitorInstance>::destroyInstance,
    &MonitorApi<MonitorInstance>::startMonitor,
    &MonitorApi<MonitorInstance>::stopMonitor,
    &MonitorApi<MonitorInstance>::diagnostics,
    &MonitorApi<MonitorInstance>::diagnostics_json,
};

}
