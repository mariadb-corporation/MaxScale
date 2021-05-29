/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxbase/json.hh>
#include <maxscale/mainworker.hh>
#include <maxsql/mariadb_connector.hh>

#include <stdexcept>

namespace maxscale
{

class ConfigManager
{
public:

    // The primary key must be under 3072 bytes which for the utf8_mb4 character set is 768 characters. Having
    // the limit as 256 characters should be enough for almost all cases as that's the maximum length of a
    // hostname which some people seem to use for the object names.
    static constexpr int CLUSTER_MAX_LEN = 256;

    /**
     * Get the current configuration manager
     */
    static ConfigManager* get();

    /**
     * Create a new configuration manager
     */
    ConfigManager(MainWorker* main_worker);

    ~ConfigManager();

    /**
     * Start synchronizing with the cluster
     */
    void start_sync();

    /**
     * Check if a cached configuration is available and load it if it is
     *
     * @return True if a cached configuration was loaded
     */
    bool load_cached_config();


    /**
     * Process the cached configuration from disk
     *
     * @return True if the configuration was processed successfully
     */
    bool process_cached_config();

    /**
     * Start a configuration change
     *
     * This starts a configuration change that will be synchronized with all the MaxScales that use the same
     * cluster for synchronization. If this phase of the configuration change fails, the internal state is not
     * updated. An attempt to synchronize with the cluster should be made when a failure occurs.
     *
     * @return True if the configuration change was started successfully
     */
    bool start();

    /**
     * Commit configuration change
     *
     * This stores the configuration in the cluster and, if successful, caches it locally. If this phase of
     * the
     * configuration change fails, an attempt to synchronize with the cluster must be made as the internal
     * state
     * has possibly deviated from the rest of the cluster.
     *
     * @return True if the configuration change was committed successfully.
     */
    bool commit();

    /**
     * Roll back the current configuration change
     *
     * If the configuration change fails on the local node, the configuration change must be rolled back.
     */
    void rollback();
private:

    struct Exception : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    enum class Type
    {
        SERVERS, MONITORS, SERVICES, LISTENERS, FILTERS, MAXSCALE, UNKNOWN
    };

    template<class T>
    std::string args_to_string(std::ostringstream& ss, T t) const
    {
        ss << t;
        return ss.str();
    }

    template<class T, class ... Args>
    std::string args_to_string(std::ostringstream& ss, T t, Args ... args) const
    {
        ss << t;
        return args_to_string(ss, args ...);
    }

    template<class ... Args>
    Exception error(Args ... args) const
    {
        std::ostringstream ss;
        return Exception(args_to_string(ss, args ...));
    }

    Type        to_type(const std::string& type);
    std::string dynamic_config_filename() const;

    void process_config(mxb::Json&& new_json);
    void remove_old_object(const std::string& name, const std::string& type);
    void create_new_object(const std::string& name, const std::string& type, mxb::Json& obj);
    void update_object(const std::string& name, const std::string& type, const mxb::Json& json);

    mxb::Json create_config(int64_t version);
    void      remove_extra_data(json_t* data);
    void      append_config(json_t* arr, json_t* json);

    const std::string& cluster_name() const;

    void    connect();
    void    verify_sync();
    void    update_config(const std::string& payload);
    SERVER* get_server() const;

    void      sync();
    void      queue_sync();
    mxb::Json fetch_config();

    mxs::MainWorker* m_worker {nullptr};

    // Helper object for storing temporary data
    mxb::Json m_tmp {mxb::Json::Type::OBJECT};

    // The latest configuration that was either created or loaded
    mxb::Json m_current_config {mxb::Json::Type::NONE};

    // The latest processed configuration version
    int64_t m_version {0};

    mxq::MariaDB m_conn;
    bool         m_row_exists {false};
    SERVER*      m_server {nullptr};
    uint32_t     m_dcid {0};

    bool m_log_sync_error {true};
    bool m_log_stale_cluster {true};
};
}
