/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <map>
#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>

/**
 * qc_init_kind_t specifies what kind of initialization should be performed.
 */
enum qc_init_kind_t
{
    QC_INIT_SELF   = 0x01,  /*< Initialize/finalize the query classifier itself. */
    QC_INIT_PLUGIN = 0x02,  /*< Initialize/finalize the plugin. */
    QC_INIT_BOTH   = 0x03
};

/**
 * QUERY_CLASSIFIER defines the object a query classifier plugin must
 * implement and return.
 *
 * To a user of the query classifier functionality, it can in general
 * be ignored.
 */
struct QUERY_CLASSIFIER
{
    /**
     * Called once to setup the query classifier
     *
     * @param sql_mode  The default sql mode.
     * @param args      The value of `query_classifier_args` in the configuration file.
     *
     * @return QC_RESULT_OK, if the query classifier could be setup, otherwise
     *         some specific error code.
     */
    int32_t (* qc_setup)(qc_sql_mode_t sql_mode, const char* args);

    /**
     * Called once at process startup. Typically not required, as the standard module loader already
     * calls this function through the module interface.
     *
     * @return QC_RESULT_OK, if the process initialization succeeded.
     */
    int32_t (* qc_process_init)(void);

    /**
     * Called once at process shutdown.
     */
    void (* qc_process_end)(void);

    /**
     * Called once per each thread.
     *
     * @return QC_RESULT_OK, if the thread initialization succeeded.
     */
    int32_t (* qc_thread_init)(void);

    /**
     * Called once when a thread finishes.
     */
    void (* qc_thread_end)(void);

    /**
     * Called to explicitly parse a statement.
     *
     * @param stmt     The statement to be parsed.
     * @param collect  A bitmask of @c qc_collect_info_t values. Specifies what information
     *                 should be collected. Only a hint and must not restrict what information
     *                 later can be queried.
     * @param result   On return, the parse result, if @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_parse)(GWBUF* stmt, uint32_t collect, int32_t* result);

    /**
     * Reports the type of the statement.
     *
     * @param stmt  A COM_QUERY or COM_STMT_PREPARE packet.
     * @param type  On return, the type mask (combination of @c qc_query_type_t),
     *              if @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_type_mask)(GWBUF* stmt, uint32_t* type);

    /**
     * Reports the operation of the statement.
     *
     * @param stmt  A COM_QUERY or COM_STMT_PREPARE packet.
     * @param type  On return, the operation (one of @c qc_query_op_t), if
     *              @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_operation)(GWBUF* stmt, int32_t* op);

    /**
     * Reports the name of a created table.
     *
     * @param stmt  A COM_QUERY or COM_STMT_PREPARE packet.
     * @param name  On return, the name of the created table, if
     *              @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_created_table_name)(GWBUF* stmt, char** name);

    /**
     * Reports whether a statement is a "DROP TABLE ..." statement.
     *
     * @param stmt           A COM_QUERY or COM_STMT_PREPARE packet
     * @param is_drop_table  On return, non-zero if the statement is a DROP TABLE
     *                       statement, if @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_is_drop_table_query)(GWBUF* stmt, int32_t* is_drop_table);

    /**
     * Returns all table names.
     *
     * @param stmt       A COM_QUERY or COM_STMT_PREPARE packet.
     * @param fullnames  If non-zero, the full (i.e. qualified) names are returned.
     * @param names      On return, the names of the statement, if @c QC_RESULT_OK
     *                   is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_table_names)(GWBUF* stmt, int32_t full_names, std::vector<std::string>* names);

    /**
     * The canonical version of a statement.
     *
     * @param stmt       A COM_QUERY or COM_STMT_PREPARE packet.
     * @param canonical  On return, the canonical version of the statement, if @c QC_RESULT_OK
     *                   is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_canonical)(GWBUF* stmt, char** canonical);

    /**
     * Reports whether the statement has a where clause.
     *
     * @param stmt        A COM_QUERY or COM_STMT_PREPARE packet.
     * @param has_clause  On return, non-zero if the statement has a where clause, if
     *                    @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_query_has_clause)(GWBUF* stmt, int32_t* has_clause);

    /**
     * Reports the database names.
     *
     * @param stmt   A COM_QUERY or COM_STMT_PREPARE packet.
     * @param names  On return, the database names, if
     *               @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_database_names)(GWBUF* stmt, std::vector<std::string>* names);

    /**
     * Reports the prepare name.
     *
     * @param stmt  A COM_QUERY or COM_STMT_PREPARE packet.
     * @param name  On return, the name of a prepare statement, if
     *              @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_prepare_name)(GWBUF* stmt, char** name);

    /**
     * Reports field information.
     *
     * @param stmt    A COM_QUERY or COM_STMT_PREPARE packet.
     * @param infos   On return, array of field infos, if @c QC_RESULT_OK is returned.
     * @param n_infos On return, the size of @c infos, if @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_field_info)(GWBUF* stmt, const QC_FIELD_INFO** infos, uint32_t* n_infos);

    /**
     * The canonical version of a statement.
     *
     * @param stmt  A COM_QUERY or COM_STMT_PREPARE packet.
     * @param infos   On return, array of function infos, if @c QC_RESULT_OK is returned.
     * @param n_infos On return, the size of @c infos, if @c QC_RESULT_OK is returned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_function_info)(GWBUF* stmt, const QC_FUNCTION_INFO** infos, uint32_t* n_infos);

    /**
     * Return the preparable statement of a PREPARE statement.
     *
     * @param stmt             A COM_QUERY or COM_STMT_PREPARE packet.
     * @param preparable_stmt  On return, the preparable statement (provided @c stmt is a
     *                         PREPARE statement), if @c QC_RESULT_OK is returned. Otherwise
     *                         NULL.
     *
     * @attention The returned GWBUF is the property of @c stmt and will be deleted when
     *            @c stmt is. If the preparable statement need to be retained beyond the
     *            lifetime of @c stmt, it must be cloned.
     *
     * @return QC_RESULT_OK, if the parsing was not aborted due to resource
     *         exhaustion or equivalent.
     */
    int32_t (* qc_get_preparable_stmt)(GWBUF* stmt, GWBUF** preparable_stmt);

    /**
     * Set the version of the server. The version may affect how a statement
     * is classified. Note that the server version is maintained separately
     * for each thread.
     *
     * @param version  Version encoded as MariaDB encodes the version, i.e.:
     *                 version = major * 10000 + minor * 100 + patch
     */
    void (* qc_set_server_version)(uint64_t version);

    /**
     * Get the thread specific version assumed of the server. If the version has
     * not been set, all values are 0.
     *
     * @param version  The version encoded as MariaDB encodes the version, i.e.:
     *                 version = major * 10000 + minor * 100 + patch
     */
    void (* qc_get_server_version)(uint64_t* version);

    /**
     * Gets the sql mode of the *calling* thread.
     *
     * @param sql_mode  The mode.
     *
     * @return QC_RESULT_OK
     */
    int32_t (* qc_get_sql_mode)(qc_sql_mode_t* sql_mode);

    /**
     * Sets the sql mode for the *calling* thread.
     *
     * @param sql_mode  The mode.
     *
     * @return QC_RESULT_OK if @sql_mode is valid, otherwise QC_RESULT_ERROR.
     */
    int32_t (* qc_set_sql_mode)(qc_sql_mode_t sql_mode);

    /**
     * Dups the provided info object. After having been dupped, the info object
     * can be stored on another GWBUF.
     *
     * @param info  The info to be dupped.
     *
     * @return The same info that was provided as argument.
     */
    QC_STMT_INFO* (* qc_info_dup)(QC_STMT_INFO* info);

    /**
     * Closes a dupped info object. After the info object has been closed, it must
     * not be accessed.
     *
     * @param info  The info to be closed.
     */
    void (* qc_info_close)(QC_STMT_INFO* info);

    /**
     * Gets the options of the *calling* thread.
     *
     * @return Bit mask of values from qc_option_t.
     */
    uint32_t (* qc_get_options)();

    /**
     * Sets the options for the *calling* thread.
     *
     * @param options Bits from qc_option_t.
     *
     * @return QC_RESULT_OK if @c options is valid, otherwise QC_RESULT_ERROR.
     */
    int32_t (* qc_set_options)(uint32_t options);

    /**
     * Get result from info.
     *
     * @param  The info whose result should be returned.
     *
     * @return The result of the provided info.
     */
    QC_STMT_RESULT (* qc_get_result_from_info)(const QC_STMT_INFO* info);

    /**
     * Return statement currently being classified.
     *
     * @param ppStmp  Pointer to pointer that on return will point to the
     *                statement being classified.
     * @param pLen    Pointer to value that on return will contain the length
     *                of the returned string.
     *
     * @return QC_RESULT_OK if a statement was returned (i.e. a statement is being
     *         classified), QC_RESULT_ERROR otherwise.
     */
    int32_t (* qc_get_current_stmt)(const char** ppStmt, size_t* pLen);
};

/**
 * QC_CACHE_PROPERTIES specifies the limits of the query classification cache.
 */
struct QC_CACHE_PROPERTIES
{
    int64_t max_size;   /** The maximum size of the cache. */
};

/**
 * QC_CACHE_STATS provides statistics of the cache.
 */
struct QC_CACHE_STATS
{
    int64_t size;       /** The current size of the cache. */
    int64_t inserts;    /** The number of inserts. */
    int64_t hits;       /** The number of hits. */
    int64_t misses;     /** The number of misses. */
    int64_t evictions;  /** The number of evictions. */
};

/**
 * Loads and sets up the default query classifier.
 *
 * This must be called once during the execution of a process. The query
 * classifier functions can only be used if this function first and thereafter
 * the @c qc_process_init return true.
 *
 * MaxScale calls this function, so plugins should not do that.
 *
 * @param cache_properties  If non-NULL, specifies the properties of the QC cache.
 * @param sql_mode          The default sql mode.
 * @param plugin_name       The name of the plugin from which the query classifier
 *                          should be loaded.
 * @param plugin_args       The arguments to be provided to the query classifier.
 *
 * @return True if the query classifier could be loaded and initialized,
 *         false otherwise.
 *
 * @see qc_process_init qc_thread_init
 */
bool qc_setup(const QC_CACHE_PROPERTIES* cache_properties,
              qc_sql_mode_t sql_mode,
              const char* plugin_name,
              const char* plugin_args);

/**
 * Loads and setups the default query classifier, and performs
 * process and thread initialization.
 *
 * This is primary intended for making the setup of stand-alone
 * test-programs simpler.
 *
 * @param cache_properties  If non-NULL, specifies the properties of the QC cache.
 * @param sql_mode          The default sql mode.
 * @param plugin_name       The name of the plugin from which the query classifier
 *                          should be loaded.
 * @param plugin_args       The arguments to be provided to the query classifier.
 *
 * @return True if the query classifier could be loaded and initialized,
 *         false otherwise.
 *
 * @see qc_end.
 */
bool qc_init(const QC_CACHE_PROPERTIES* cache_properties,
             qc_sql_mode_t sql_mode,
             const char* plugin_name,
             const char* plugin_args);

/**
 * Performs thread and process finalization.
 *
 * This is primary intended for making the tear-down of stand-alone
 * test-programs simpler.
 */
void qc_end();

/**
 * Intializes the query classifier.
 *
 * This function should be called once, provided @c qc_setup returned true,
 * before the query classifier functionality is used.
 *
 * MaxScale calls this functions, so plugins should not do that.
 *
 * @param kind  What kind of initialization should be performed.
 *              Combination of qc_init_kind_t.
 *
 * @return True, if the process wide initialization could be performed.
 *
 * @see qc_process_end qc_thread_init
 */
bool qc_process_init(uint32_t kind);

/**
 * Finalizes the query classifier.
 *
 * A successful call of @c qc_process_init should before program exit be
 * followed by a call to this function. MaxScale calls this function, so
 * plugins should not do that.
 *
 * @param kind  What kind of finalization should be performed.
 *              Combination of qc_init_kind_t.
 *
 * @see qc_process_init qc_thread_end
 */
void qc_process_end(uint32_t kind);

/**
 * Loads a particular query classifier.
 *
 * In general there is no need to use this function, but rely upon qc_init().
 * However, if there is a need to use multiple query classifiers concurrently
 * then this function provides the means for that. Note that after a query
 * classifier has been loaded, it must explicitly be initialized before it
 * can be used.
 *
 * @param plugin_name  The name of the plugin from which the query classifier
 *                     should be loaded.
 *
 * @return A QUERY_CLASSIFIER object if successful, NULL otherwise.
 *
 * @see qc_unload
 */
QUERY_CLASSIFIER* qc_load(const char* plugin_name);

/**
 * Unloads an explicitly loaded query classifier.
 *
 * @see qc_load
 */
void qc_unload(QUERY_CLASSIFIER* classifier);

/**
 * Performs thread initialization needed by the query classifier. Should
 * be called in every thread.
 *
 * MaxScale calls this function, so plugins should not do that.
 *
 * @param kind  What kind of initialization should be performed.
 *              Combination of qc_init_kind_t.
 *
 * @return True if the initialization succeeded, false otherwise.
 *
 * @see qc_thread_end
 */
bool qc_thread_init(uint32_t kind);

/**
 * Performs thread finalization needed by the query classifier.
 * A successful call to @c qc_thread_init should at some point be
 * followed by a call to this function.
 *
 * MaxScale calls this function, so plugins should not do that.
 *
 * @param kind  What kind of finalization should be performed.
 *              Combination of qc_init_kind_t.
 *
 * @see qc_thread_init
 */
void qc_thread_end(uint32_t kind);

/**
 * Enable or disable the query classifier cache on this thread
 *
 * @param enabled If set to true, the cache is enabled. If set to false, the cache id disabled.
 */
void qc_use_local_cache(bool enabled);

/**
 * Get cache statistics for the calling thread.
 *
 * @param stats[out]  Cache statistics.
 *
 * @return True, if caching is enabled, false otherwise.
 */
bool qc_get_cache_stats(QC_CACHE_STATS* stats);

/**
 * Get cache statistics for the calling thread.
 *
 * @return An object if caching is enabled, NULL otherwise.
 */
json_t* qc_get_cache_stats_as_json();

/**
 * Get the cache properties.
 *
 * @param[out] properties  Cache properties.
 */
void qc_get_cache_properties(QC_CACHE_PROPERTIES* properties);

/**
 * Set the cache properties.
 *
 * @param properties  Cache properties.
 *
 * @return True, if the properties could be set, false if at least
 *         one property is invalid or if the combination of property
 *         values is invalid.
 */
bool qc_set_cache_properties(const QC_CACHE_PROPERTIES* properties);

/**
 * Public interface to query classifier cache state.
 */
struct QC_CACHE_ENTRY
{
    int64_t        hits;
    QC_STMT_RESULT result;
};

/**
 * Obtain query classifier cache information for the @b calling thread.
 *
 * @param state  Map where information is added.
 *
 * @note Calling with a non-empty @c state means that a cumulative result
 *       will be obtained, that is, the hits of a particular key will
 *       be added the hits of that key if it already is in the map.
 */
void qc_get_cache_state(std::map<std::string, QC_CACHE_ENTRY>& state);

/**
 * Return statement currently being classified.
 *
 * @param ppStmp  Pointer to pointer that on return will point to the
 *                statement being classified.
 * @param pLen    Pointer to value that on return will contain the length
 *                of the returned string.
 *
 * @return True, if a statement was returned (i.e. a statement is being
 *         classified), false otherwise.
 *
 * @note A string /may/ be returned /only/ when this function is called from
 *       a signal handler that is called due to the classifier causing
 *       a crash.
 */
bool qc_get_current_stmt(const char** ppStmt, size_t* pLen);

/**
 * Common query classifier properties as JSON.
 *
 * @param zHost  The MaxScale host.
 *
 * @return A json object containing properties.
 */
std::unique_ptr<json_t> qc_as_json(const char* zHost);

/**
 * Alter common query classifier properties.
 *
 * @param pJson  A JSON object.
 *
 * @return True, if the object was valid and parameters could be changed,
 *         false otherwise.
 */
bool qc_alter_from_json(json_t* pJson);

/**
 * Return query classifier cache content.
 *
 * @param zHost      The MaxScale host.
 *
 * @return A json object containing information about the query classifier cache.
 */
std::unique_ptr<json_t> qc_cache_as_json(const char* zHost);
