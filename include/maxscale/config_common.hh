/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file include/maxscale/config.h The configuration handling elements
 */

#include <maxscale/ccdefs.hh>

#include <unordered_map>
#include <string>
#include <limits.h>
#include <map>
#include <openssl/sha.h>
#include <sys/utsname.h>
#include <time.h>
#include <vector>

#include <maxbase/jansson.h>
#include <maxscale/modinfo.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>

class SERVICE;
class SERVER;

namespace maxscale
{
class Target;
}

// A mapping from a path to a percentage, e.g.: "/disk" -> 80.
using DiskSpaceLimits = std::unordered_map<std::string, int32_t>;

/** Default port where the REST API listens */
#define DEFAULT_ADMIN_HTTP_PORT 8989
#define DEFAULT_ADMIN_HOST      "127.0.0.1"

#define RELEASE_STR_LENGTH 256
#define SYSNAME_LEN        256
#define MAX_ADMIN_USER_LEN 1024
#define MAX_ADMIN_PW_LEN   1024
#define MAX_ADMIN_HOST_LEN 1024

/** JSON Pointers to key parts of JSON objects */
#define MXS_JSON_PTR_DATA       "/data"
#define MXS_JSON_PTR_ID         "/data/id"
#define MXS_JSON_PTR_TYPE       "/data/type"
#define MXS_JSON_PTR_PARAMETERS "/data/attributes/parameters"

/** Pointers to relation lists */
#define MXS_JSON_PTR_RELATIONSHIPS          "/data/relationships"
#define MXS_JSON_PTR_RELATIONSHIPS_SERVERS  "/data/relationships/servers/data"
#define MXS_JSON_PTR_RELATIONSHIPS_SERVICES "/data/relationships/services/data"
#define MXS_JSON_PTR_RELATIONSHIPS_MONITORS "/data/relationships/monitors/data"
#define MXS_JSON_PTR_RELATIONSHIPS_FILTERS  "/data/relationships/filters/data"

/** Parameter value JSON Pointers */
#define MXS_JSON_PTR_PARAM_PORT                  MXS_JSON_PTR_PARAMETERS "/port"
#define MXS_JSON_PTR_PARAM_ADDRESS               MXS_JSON_PTR_PARAMETERS "/address"
#define MXS_JSON_PTR_PARAM_SOCKET                MXS_JSON_PTR_PARAMETERS "/socket"
#define MXS_JSON_PTR_PARAM_PROTOCOL              MXS_JSON_PTR_PARAMETERS "/protocol"
#define MXS_JSON_PTR_PARAM_AUTHENTICATOR         MXS_JSON_PTR_PARAMETERS "/authenticator"
#define MXS_JSON_PTR_PARAM_AUTHENTICATOR_OPTIONS MXS_JSON_PTR_PARAMETERS "/authenticator_options"
#define MXS_JSON_PTR_PARAM_SSL_KEY               MXS_JSON_PTR_PARAMETERS "/ssl_key"
#define MXS_JSON_PTR_PARAM_SSL_CERT              MXS_JSON_PTR_PARAMETERS "/ssl_cert"
#define MXS_JSON_PTR_PARAM_SSL_CA_CERT           MXS_JSON_PTR_PARAMETERS "/ssl_ca_cert"
#define MXS_JSON_PTR_PARAM_SSL_VERSION           MXS_JSON_PTR_PARAMETERS "/ssl_version"
#define MXS_JSON_PTR_PARAM_SSL_CERT_VERIFY_DEPTH MXS_JSON_PTR_PARAMETERS "/ssl_cert_verify_depth"
#define MXS_JSON_PTR_PARAM_SSL_VERIFY_PEER_CERT  MXS_JSON_PTR_PARAMETERS "/ssl_verify_peer_certificate"
#define MXS_JSON_PTR_PARAM_SSL_VERIFY_PEER_HOST  MXS_JSON_PTR_PARAMETERS "/ssl_verify_peer_host"

/** Non-parameter JSON pointers */
#define MXS_JSON_PTR_ROUTER   "/data/attributes/router"
#define MXS_JSON_PTR_MODULE   "/data/attributes/module"
#define MXS_JSON_PTR_PASSWORD "/data/attributes/password"
#define MXS_JSON_PTR_ACCOUNT  "/data/attributes/account"

namespace maxscale
{

namespace config
{

enum DurationInterpretation
{
    INTERPRET_AS_SECONDS,
    INTERPRET_AS_MILLISECONDS,
    NO_INTERPRETATION
};

enum DurationUnit
{
    DURATION_IN_HOURS,
    DURATION_IN_MINUTES,
    DURATION_IN_SECONDS,
    DURATION_IN_MILLISECONDS,
    DURATION_IN_DEFAULT
};
}

namespace cfg = config;

/**
 * Config parameter container. Typically includes all parameters of a single configuration file section
 * such as a server or filter.
 */
class ConfigParameters
{
public:
    using ContainerType = std::map<std::string, std::string>;

    /**
     * Convert JSON object into mxs::ConfigParameters
     *
     * Only scalar values are converted into their string form.
     *
     * @param JSON object to convert
     *
     * @return the ConfigParameters representation of the object
     */
    static ConfigParameters from_json(json_t* json);

    /**
     * Get value of key as string.
     *
     * @param key Parameter name
     * @return Parameter value. Empty string if key not found.
     */
    std::string get_string(const std::string& key) const;

    /**
     * Get an integer value. Should be used for both MXS_MODULE_PARAM_INT and MXS_MODULE_PARAM_COUNT
     * parameter types.
     *
     * @param key Parameter name
     * @return Parameter parsed to integer. 0 if key was not found.
     */
    int64_t get_integer(const std::string& key) const;

    /**
     * Get a enumeration value.
     *
     * @param key Parameter name
     * @param enum_mapping Enum string->integer mapping
     * @return The enumeration value converted to an int or -1 if the parameter was not found
     *
     * @note The enumeration values should not use -1 so that an undefined parameter is
     * detected. If -1 is used, config_get_param() should be used to detect whether
     * the parameter exists
     */
    int64_t get_enum(const std::string& key, const MXS_ENUM_VALUE* enum_mapping) const;

    /**
     * @brief Get a boolean value
     *
     * The existence of the parameter should be checked with config_get_param() before
     * calling this function to determine whether the return value represents an existing
     * value or a missing value.
     *
     * @param key Parameter name
     * @return The value as a boolean or false if none was found
     */
    bool get_bool(const std::string& key) const;

    /**
     * @brief Get a size in bytes
     *
     * The value can have either one of the IEC binary prefixes or SI prefixes as
     * a suffix. For example, the value 1Ki will be converted to 1024 bytes whereas
     * 1k will be converted to 1000 bytes. Supported SI suffix values are k, m, g and t
     * in both lower and upper case. Supported IEC binary suffix values are
     * Ki, Mi, Gi and Ti both in upper and lower case.
     *
     * @param key Parameter name
     * @return Number of bytes or 0 if no parameter was found
     */
    uint64_t get_size(const std::string& key) const;

    /**
     * @brief Get a duration.
     *
     * Should be used for MXS_MODULE_PARAM_DURATION parameter types.
     *
     * @param key             Parameter name.
     * @param interpretation  How a value NOT having a unit suffix should be interpreted.
     *
     * @return Duration in milliseconds; 0 if the parameter is not found.
     */
    std::chrono::milliseconds get_duration_in_ms(const std::string& key,
                                                 mxs::config::DurationInterpretation interpretation) const;

    /**
     * @brief Get a duration in a specific unit.
     *
     * @param key  Parameter name
     *
     * @return The duration in the desired unit.
     *
     * @note The type the function is specialized with dictates how values without a
     *       suffix should be interpreted; if @c std::chrono::seconds, they will be
     *       interpreted as seconds, if @c std::chrono::milliseconds, they will be
     *       interpreted as milliseconds.
     *
     * @note There is no default implementation, but only specializations for
     *       @c std::chrono::seconds and @c std::chrono::milliseconds.
     */
    template<class T>
    T get_duration(const std::string& key) const = delete;

    /**
     * @brief Get a target value
     *
     * @param key Parameter name
     *
     * @return Pointer to target
     */
    mxs::Target* get_target(const std::string& key) const;


    /**
     * Get a list of targets
     *
     * @param key Parameter name
     *
     * @return List of found servers
     */
    std::vector<mxs::Target*> get_target_list(const std::string& key) const;

    /**
     * @brief Get a service value
     *
     * @param key Parameter name
     * @return Pointer to configured service
     */
    SERVICE* get_service(const std::string& key) const;

    /**
     * @brief Get a server value
     *
     * @param key Parameter name
     * @return Pointer to configured server
     */
    SERVER* get_server(const std::string& key) const;

    /**
     * Get an array of servers. The value is expected to be a comma-separated list of server names.
     *
     * @param key Parameter name
     * @param name_error_out If a server name was not found, it is written here. Only the first such name
     * is written.
     * @return Found servers. If even one server name was invalid, the array will be empty.
     */
    std::vector<SERVER*> get_server_list(const std::string& key, std::string* name_error_out = nullptr) const;

    /**
     * Get a compiled regular expression and the ovector size of the pattern. The
     * return value should be freed by the caller.
     *
     * @param key Parameter name
     * @param options PCRE2 compilation options
     * @param output_ovec_size Output for match data ovector size. On error,
     * nothing is written. If NULL, the parameter is ignored.
     * @return Compiled regex code on success. NULL if key was not found or compilation failed.
     */
    std::unique_ptr<pcre2_code>
    get_compiled_regex(const std::string& key, uint32_t options, uint32_t* output_ovec_size) const;

    /**
     * Get multiple compiled regular expressions and the maximum ovector size of
     * the patterns. The returned regex codes should be freed by the caller.
     *
     * @param keys An array of parameter keys.
     * @param options PCRE2 compilation options
     * @param ovec_size_out If not null, the maximum ovector size of successfully
     * compiled patterns is written here.
     * @param compile_error_out If not null, is set to true if a pattern compilation failed.
     * @return Array of compiled patterns, one element for each key. If a key is not found or the pattern
     * cannot be compiled, the corresponding element will be null.
     */
    std::vector<std::unique_ptr<pcre2_code>>
    get_compiled_regexes(const std::vector<std::string>& keys, uint32_t options,
                         uint32_t* ovec_size_out, bool* compile_error_out);

    /**
     * Check if a key exists.
     *
     * @param key Parameter name
     * @return True if key was found
     */
    bool contains(const std::string& key) const;

    /**
     * Check if any of the given keys are defined
     *
     * @param keys The keys to check
     *
     * @return True if at least one of the keys is defined
     */
    bool contains_any(const std::initializer_list<std::string>& keys) const
    {
        return std::any_of(keys.begin(), keys.end(), [this](const std::string& a) {
                               return contains(a);
                           });
    }

    /**
     * Check if all of the given keys are defined
     *
     * @param keys The keys to check
     *
     * @return True if all of the keys are defined
     */
    bool contains_all(const std::initializer_list<std::string>& keys) const
    {
        return std::all_of(keys.begin(), keys.end(), [this](const std::string& a) {
                               return contains(a);
                           });
    }

    /**
     * Set a key-value combination. If the key doesn't exist, it is added. The function is static
     * to handle the special case of params being empty. This is needed until the config management
     * has been properly refactored.
     *
     * @param key Parameter key
     * @param value Value to set
     */
    void set(const std::string& key, const std::string& value);

    /**
     * Copy all key-value pairs from a set to this container. If a key doesn't exist, it is added.
     *
     * @param source Parameters to copy
     */
    void set_multiple(const mxs::ConfigParameters& source);

    void set_from_list(std::vector<std::pair<std::string, std::string>> list,
                       const MXS_MODULE_PARAM* module_params = NULL);

    /**
     * Remove a key-value pair from the container.
     *
     * @param key Key to remove
     */
    void remove(const std::string& key);

    void clear();
    bool empty() const;

    ContainerType::const_iterator begin() const;
    ContainerType::const_iterator end() const;

private:
    ContainerType m_contents;
};

template<>
inline std::chrono::milliseconds
mxs::ConfigParameters::get_duration<std::chrono::milliseconds>(const std::string& key) const
{
    return get_duration_in_ms(key, mxs::config::INTERPRET_AS_MILLISECONDS);
}

template<>
inline std::chrono::seconds
mxs::ConfigParameters::get_duration<std::chrono::seconds>(const std::string& key) const
{
    std::chrono::milliseconds ms = get_duration_in_ms(key, mxs::config::INTERPRET_AS_SECONDS);
    return std::chrono::duration_cast<std::chrono::seconds>(ms);
}
}


/**
 * The config context structure, used to build the configuration
 * data during the parse process
 */
class CONFIG_CONTEXT
{
public:
    CONFIG_CONTEXT(const std::string& section = "");

    std::string           m_name;           /**< The name of the object being configured */
    mxs::ConfigParameters m_parameters;     /**< The list of parameter values */
    bool                  m_was_persisted;  /**< True if this object was persisted */
    CONFIG_CONTEXT*       m_next {nullptr}; /**< Next pointer in the linked list */

    const char* name() const
    {
        return m_name.c_str();
    }
};

/**
 * @brief Check if a configuration parameter is valid
 *
 * If a module has declared parameters and parameters were given to the module,
 * the given parameters are compared to the expected ones. This function also
 * does preliminary type checking for various basic values as well as enumerations.
 *
 * @param params Module parameters
 * @param key Parameter key
 * @param value Parameter value
 * @param context Configuration context or NULL for no context (uses runtime checks)
 *
 * @return True if the configuration parameter is valid
 */
bool config_param_is_valid(const MXS_MODULE_PARAM* params,
                           const char* key,
                           const char* value,
                           const CONFIG_CONTEXT* context);

/**
 * Break a comma-separated list into a string array. Removes whitespace from list items.
 *
 * @param list_string A list of items
 * @return The array
 */
std::vector<std::string> config_break_list_string(const std::string& list_string);

/**
 * @brief Convert string truth value
 *
 * Used for truth values with @c 1, @c yes or @c true for a boolean true value and @c 0, @c no
 * or @c false for a boolean false value.
 *
 * @param str String to convert to a truth value
 *
 * @return 1 if @c value is true, 0 if value is false and -1 if the value is not
 * a valid truth value
 */
int config_truth_value(const char* value);
inline int config_truth_value(const std::string& value)
{
    return config_truth_value(value.c_str());
}

/**
 * @brief Get worker thread count
 *
 * @return Number of worker threads
 */
int config_threadcount(void);


/**
 * @brief List all path parameters as JSON
 *
 * @param host Hostname of this server
 * @return JSON object representing the paths used by MaxScale
 */
json_t* config_maxscale_to_json(const char* host);

/**
 * @brief  Get DCB write queue high water mark
 *
 * @return  Number of high water mark in bytes
 */
uint32_t config_writeq_high_water();

/**
 * @brief  Get DCB write queue low water mark
 *
 * @return @return  Number of low water mark in bytes
 */
uint32_t config_writeq_low_water();

/**
 * @brief Interpret a @disk_space_threshold configuration string.
 *
 * @param disk_space_threshold  Data structure for holding disk space configuration.
 * @param config_value          Configuration value from the configuration file.
 *
 * @return True, if @ config_value was valid, false otherwise.
 *
 */
bool config_parse_disk_space_threshold(DiskSpaceLimits* disk_space_threshold,
                                       const char* config_value);

/**
 * @brief Check whether section/object name is valid.
 *
 * @param name     The name to be checked.
 * @param reason   If non-null, will in case the name is not valid contain
 *                 the reason when the function returns.
 *
 * @return True, if the name is valid, false otherwise.
 */
bool config_is_valid_name(const char* name, std::string* reason = nullptr);

inline bool config_is_valid_name(const std::string& name, std::string* reason = nullptr)
{
    return config_is_valid_name(name.c_str());
}

// TEMPORARILY EXPOSED.
bool check_path_parameter(const MXS_MODULE_PARAM* params, const char* value);

/**
 * Converts a string into milliseconds, intepreting in a case-insensitive manner
 * an 'h'-suffix to indicate hours, an 'm'-suffix to indicate minutes, an
 * 's'-suffix to indicate seconds and an 'ms'-suffix to indicate milliseconds.
 *
 * @param zValue          A numerical string, possibly suffixed by 'h', 'm',
 *                        's' or 'ms'.
 * @param interpretation  How a value lacking a specific suffix should be interpreted.
 * @param pDuration       Pointer, if non-NULL, where the result is stored.
 * @param pUnit           Pointer, if non-NULL, where the detected unit is stored.
 *
 * @return True on success, false on invalid input in which case @c pUnit and
 *         @c pDuration will not be modified.
 */
bool get_suffixed_duration(const char* zValue,
                           mxs::config::DurationInterpretation interpretation,
                           std::chrono::milliseconds* pDuration,
                           mxs::config::DurationUnit* pUnit = nullptr);

/**
 * Converts a string into milliseconds, intepreting in a case-insensitive manner
 * an 'h'-suffix to indicate hours, an 'm'-suffix to indicate minutes, an
 * 's'-suffix to indicate seconds and an 'ms'-suffix to indicate milliseconds.
 *
 * A value lacking a specific suffix will be interpreted as milliseconds.
 *
 * @param zValue     A numerical string, possibly suffixed by 'h', 'm',
 *                   's' or 'ms'.
 * @param pDuration  Pointer, if non-NULL, where the result is stored.
 * @param pUnit      Pointer, if non-NULL, where the detected unit is stored.
 *
 * @return True on success, false on invalid input in which case @c pUnit and
 *         @c pDuration will not be modified.
 */
inline bool get_suffixed_duration(const char* zValue,
                                  std::chrono::milliseconds* pDuration,
                                  mxs::config::DurationUnit* pUnit = nullptr)
{
    return get_suffixed_duration(zValue, mxs::config::INTERPRET_AS_MILLISECONDS, pDuration, pUnit);
}

/**
 * Converts a string into seconds, intepreting in a case-insensitive manner
 * an 'h'-suffix to indicate hours, an 'm'-suffix to indicate minutes, an
 * 's'-suffix to indicate seconds and an 'ms'-suffix to indicate milliseconds.
 *
 * A value lacking a specific suffix will be interpreted as seconds.
 *
 * @param zValue     A numerical string, possibly suffixed by 'h', 'm',
 *                   's' or 'ms'.
 * @param pDuration  Pointer, if non-NULL, where the result is stored.
 * @param pUnit      Pointer, if non-NULL, where the detected unit is stored.
 *
 * @return True on success, false on invalid input in which case @c pUnit and
 *         @c pDuration will not be modified.
 */
inline bool get_suffixed_duration(const char* zValue,
                                  std::chrono::seconds* pDuration,
                                  mxs::config::DurationUnit* pUnit = nullptr)
{
    std::chrono::milliseconds ms;

    bool rv = get_suffixed_duration(zValue, mxs::config::INTERPRET_AS_SECONDS, &ms, pUnit);

    if (rv)
    {
        *pDuration = std::chrono::duration_cast<std::chrono::seconds>(ms);
    }

    return rv;
}

/**
 * Converts a string into the corresponding value, interpreting
 * IEC or SI prefixes used as suffixes appropriately.
 *
 * @param value A numerical string, possibly suffixed by a IEC binary prefix or
 *              SI prefix.
 * @param dest  Pointer where the result is stored. If set to NULL, only the
 *              validity of value is checked.
 *
 * @return True on success, false on invalid input in which case contents of
 *         `dest` are left in an undefined state
 */
bool get_suffixed_size(const char* value, uint64_t* dest);

inline bool get_suffixed_size(const std::string& value, uint64_t* dest)
{
    return get_suffixed_size(value.c_str(), dest);
}

/**
 * Compile a regex string using PCRE2 using the settings provided.
 *
 * @param regex_string The string to compile
 * @param jit_enabled Enable JIT compilation. If true but JIT is not available,
 * a warning is printed.
 * @param options PCRE2 compilation options
 * @param output_ovector_size Output for the match data ovector size. On error,
 * nothing is written. If NULL, the parameter is ignored.
 * @return Compiled regex code on success, NULL otherwise
 */
pcre2_code* compile_regex_string(const char* regex_string,
                                 bool jit_enabled,
                                 uint32_t options,
                                 uint32_t* output_ovector_size);
