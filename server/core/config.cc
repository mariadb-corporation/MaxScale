/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file config.c Configuration file processing
 */

#include <maxscale/config.h>
#include <maxscale/config.hh>

#include <ctype.h>
#include <ftw.h>
#include <fcntl.h>
#include <glob.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ini.h>

#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <unordered_set>

#include <maxscale/adminusers.h>
#include <maxscale/alloc.h>
#include <maxscale/clock.h>
#include <maxscale/housekeeper.h>
#include <maxscale/http.hh>
#include <maxscale/json_api.h>
#include <maxscale/limits.h>
#include <maxscale/log.h>
#include <maxscale/maxscale.h>
#include <maxscale/paths.h>
#include <maxscale/pcre2.h>
#include <maxscale/router.h>
#include <maxscale/secrets.h>
#include <maxscale/spinlock.h>
#include <maxscale/utils.h>
#include <maxscale/utils.hh>
#include <maxscale/version.h>

#include "internal/config.hh"
#include "internal/event.hh"
#include "internal/filter.hh"
#include "internal/modules.h"
#include "internal/monitor.h"
#include "internal/service.hh"

using std::set;
using std::string;

const char CN_ACCOUNT[]                       = "account";
const char CN_ADDRESS[]                       = "address";
const char CN_ARG_MAX[]                       = "arg_max";
const char CN_ARG_MIN[]                       = "arg_min";
const char CN_ADMIN_AUTH[]                    = "admin_auth";
const char CN_ADMIN_ENABLED[]                 = "admin_enabled";
const char CN_ADMIN_LOG_AUTH_FAILURES[]       = "admin_log_auth_failures";
const char CN_ADMIN_HOST[]                    = "admin_host";
const char CN_ADMIN_PORT[]                    = "admin_port";
const char CN_ADMIN_SSL_KEY[]                 = "admin_ssl_key";
const char CN_ADMIN_SSL_CERT[]                = "admin_ssl_cert";
const char CN_ADMIN_SSL_CA_CERT[]             = "admin_ssl_ca_cert";
const char CN_ATTRIBUTES[]                    = "attributes";
const char CN_AUTHENTICATOR[]                 = "authenticator";
const char CN_AUTHENTICATOR_DIAGNOSTICS[]     = "authenticator_diagnostics";
const char CN_AUTHENTICATOR_OPTIONS[]         = "authenticator_options";
const char CN_AUTH_ALL_SERVERS[]              = "auth_all_servers";
const char CN_AUTH_CONNECT_TIMEOUT[]          = "auth_connect_timeout";
const char CN_AUTH_READ_TIMEOUT[]             = "auth_read_timeout";
const char CN_AUTH_WRITE_TIMEOUT[]            = "auth_write_timeout";
const char CN_AUTO[]                          = "auto";
const char CN_CACHE_SIZE[]                    = "cache_size";
const char CN_CONNECTION_TIMEOUT[]            = "connection_timeout";
const char CN_DATA[]                          = "data";
const char CN_DEFAULT[]                       = "default";
const char CN_DESCRIPTION[]                   = "description";
const char CN_DISK_SPACE_THRESHOLD[]          = "disk_space_threshold";
const char CN_DUMP_LAST_STATEMENTS[]          = "dump_last_statements";
const char CN_ENABLE_ROOT_USER[]              = "enable_root_user";
const char CN_FILTERS[]                       = "filters";
const char CN_FILTER[]                        = "filter";
const char CN_FILTER_DIAGNOSTICS[]            = "filter_diagnostics";
const char CN_GATEWAY[]                       = "gateway";
const char CN_ID[]                            = "id";
const char CN_INET[]                          = "inet";
const char CN_LISTENER[]                      = "listener";
const char CN_LISTENERS[]                     = "listeners";
const char CN_LOCALHOST_MATCH_WILDCARD_HOST[] = "localhost_match_wildcard_host";
const char CN_LOG_AUTH_WARNINGS[]             = "log_auth_warnings";
const char CN_LOG_THROTTLING[]                = "log_throttling";
const char CN_MAXSCALE[]                      = "maxscale";
const char CN_MAX_CONNECTIONS[]               = "max_connections";
const char CN_MAX_RETRY_INTERVAL[]            = "max_retry_interval";
const char CN_META[]                          = "meta";
const char CN_METHOD[]                        = "method";
const char CN_MODULE[]                        = "module";
const char CN_MODULES[]                       = "modules";
const char CN_MODULE_COMMAND[]                = "module_command";
const char CN_MONITORS[]                      = "monitors";
const char CN_MONITOR[]                       = "monitor";
const char CN_MONITOR_DIAGNOSTICS[]           = "monitor_diagnostics";
const char CN_MS_TIMESTAMP[]                  = "ms_timestamp";
const char CN_NAME[]                          = "name";
const char CN_NON_BLOCKING_POLLS[]            = "non_blocking_polls";
const char CN_OPTIONS[]                       = "options";
const char CN_PARAMETERS[]                    = "parameters";
const char CN_PASSIVE[]                       = "passive";
const char CN_PASSWORD[]                      = "password";
const char CN_PEER_HOSTS[]                    = "peer_hosts";
const char CN_PEER_PASSWORD[]                 = "peer_password";
const char CN_PEER_USER[]                     = "peer_user";
const char CN_POLL_SLEEP[]                    = "poll_sleep";
const char CN_PORT[]                          = "port";
const char CN_PROTOCOL[]                      = "protocol";
const char CN_QUERY_CLASSIFIER[]              = "query_classifier";
const char CN_QUERY_CLASSIFIER_ARGS[]         = "query_classifier_args";
const char CN_QUERY_CLASSIFIER_CACHE_SIZE[]   = "query_classifier_cache_size";
const char CN_QUERY_RETRIES[]                 = "query_retries";
const char CN_QUERY_RETRY_TIMEOUT[]           = "query_retry_timeout";
const char CN_RELATIONSHIPS[]                 = "relationships";
const char CN_LINKS[]                         = "links";
const char CN_LOCAL_ADDRESS[]                 = "local_address";
const char CN_REQUIRED[]                      = "required";
const char CN_RETAIN_LAST_STATEMENTS[]        = "retain_last_statements";
const char CN_RETRY_ON_FAILURE[]              = "retry_on_failure";
const char CN_ROUTER[]                        = "router";
const char CN_ROUTER_DIAGNOSTICS[]            = "router_diagnostics";
const char CN_ROUTER_OPTIONS[]                = "router_options";
const char CN_SELF[]                          = "self";
const char CN_SERVERS[]                       = "servers";
const char CN_SERVER[]                        = "server";
const char CN_SERVICES[]                      = "services";
const char CN_SERVICE[]                       = "service";
const char CN_SESSIONS[]                      = "sessions";
const char CN_SESSION_TRACK_TRX_STATE[]       = "session_track_trx_state";
const char CN_SKIP_PERMISSION_CHECKS[]        = "skip_permission_checks";
const char CN_SOCKET[]                        = "socket";
const char CN_SQL_MODE[]                      = "sql_mode";
const char CN_STATE[]                         = "state";
const char CN_SSL[]                           = "ssl";
const char CN_SSL_CA_CERT[]                   = "ssl_ca_cert";
const char CN_SSL_CERT[]                      = "ssl_cert";
const char CN_SSL_CERT_VERIFY_DEPTH[]         = "ssl_cert_verify_depth";
const char CN_SSL_VERIFY_PEER_CERTIFICATE[]   = "ssl_verify_peer_certificate";
const char CN_SSL_KEY[]                       = "ssl_key";
const char CN_SSL_VERSION[]                   = "ssl_version";
const char CN_STRIP_DB_ESC[]                  = "strip_db_esc";
const char CN_SUBSTITUTE_VARIABLES[]          = "substitute_variables";
const char CN_THREADS[]                       = "threads";
const char CN_THREAD_STACK_SIZE[]             = "thread_stack_size";
const char CN_TICKS[]                         = "ticks";
const char CN_TYPE[]                          = "type";
const char CN_UNIX[]                          = "unix";
const char CN_USER[]                          = "user";
const char CN_USERS[]                         = "users";
const char CN_USERS_REFRESH_TIME[]            = "users_refresh_time";
const char CN_VERSION_STRING[]                = "version_string";
const char CN_WEIGHTBY[]                      = "weightby";
const char CN_WRITEQ_HIGH_WATER[]             = "writeq_high_water";
const char CN_WRITEQ_LOW_WATER[]              = "writeq_low_water";


extern const char CN_LOGDIR[] = "logdir";
extern const char CN_LIBDIR[] = "libdir";
extern const char CN_PIDDIR[] = "piddir";
extern const char CN_DATADIR[] = "datadir";
extern const char CN_CACHEDIR[] = "cachedir";
extern const char CN_LANGUAGE[] = "language";
extern const char CN_EXECDIR[] = "execdir";
extern const char CN_CONNECTOR_PLUGINDIR[] = "connector_plugindir";
extern const char CN_PERSISTDIR[] = "persistdir";
extern const char CN_MODULE_CONFIGDIR[] = "module_configdir";
extern const char CN_SYSLOG[] = "syslog";
extern const char CN_MAXLOG[] = "maxlog";
extern const char CN_LOG_AUGMENTATION[] = "log_augmentation";
extern const char CN_LOG_TO_SHM[] = "log_to_shm";

typedef struct duplicate_context
{
    std::set<std::string> *sections;
    pcre2_code            *re;
    pcre2_match_data      *mdata;
} DUPLICATE_CONTEXT;

static bool duplicate_context_init(DUPLICATE_CONTEXT* context);
static void duplicate_context_finish(DUPLICATE_CONTEXT* context);

static bool process_config_context(CONFIG_CONTEXT *);
static bool process_config_update(CONFIG_CONTEXT *);
static char *config_get_value(MXS_CONFIG_PARAMETER *, const char *);
static char *config_get_password(MXS_CONFIG_PARAMETER *);
static const char* config_get_value_string(const MXS_CONFIG_PARAMETER *params, const char *name);
static int handle_global_item(const char *, const char *);
static bool check_config_objects(CONFIG_CONTEXT *context);
static int maxscale_getline(char** dest, int* size, FILE* file);
static bool check_first_last_char(const char* string, char expected);
static void remove_first_last_char(char* value);
static bool test_regex_string_validity(const char* regex_string, const char* key);
static pcre2_code* compile_regex_string(const char* regex_string, bool jit_enabled,
                                        uint32_t options, uint32_t* output_ovector_size);

int config_get_ifaddr(unsigned char *output);
static int config_get_release_string(char* release);
bool config_has_duplicate_sections(const char* config, DUPLICATE_CONTEXT* context);
int create_new_service(CONFIG_CONTEXT *obj);
int create_new_server(CONFIG_CONTEXT *obj);
int create_new_monitor(CONFIG_CONTEXT *obj, std::set<std::string>& monitored_servers);
int create_new_listener(CONFIG_CONTEXT *obj);
int create_new_filter(CONFIG_CONTEXT *obj);
void config_fix_param(const MXS_MODULE_PARAM *params, MXS_CONFIG_PARAMETER *p);

static const char *config_file = NULL;
static MXS_CONFIG gateway;
char *version_string = NULL;
static bool is_persisted_config = false; /**< True if a persisted configuration file is being parsed */
static CONFIG_CONTEXT config_context;

// Values for the `ssl` parameter. These are plain boolean types but for legacy
// reasons the required and disabled keywords need to be allowed.
static const MXS_ENUM_VALUE ssl_values[] =
{
    {"required", 1},
    {"true", 1},
    {"yes", 1},
    {"on", 1},
    {"1", 1},
    {"disabled",  0},
    {"false",  0},
    {"no",  0},
    {"off",  0},
    {"0",  0},
    {NULL}
};

static const MXS_ENUM_VALUE ssl_version_values[] =
{
    {"MAX", 1},
#ifndef OPENSSL_1_1
    {"TLSv10", 1},
#endif
#ifdef OPENSSL_1_0
    {"TLSv11", 1},
    {"TLSv12", 1},
#endif
    {NULL}
};

const MXS_MODULE_PARAM config_service_params[] =
{
    {CN_TYPE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_ROUTER, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_ROUTER_OPTIONS, MXS_MODULE_PARAM_STRING},
    {CN_SERVERS, MXS_MODULE_PARAM_STRING},
    {CN_USER, MXS_MODULE_PARAM_STRING},     // Not mandatory due to RCAP_TYPE_NO_AUTH
    {CN_PASSWORD, MXS_MODULE_PARAM_STRING}, // Not mandatory due to RCAP_TYPE_NO_AUTH
    {CN_ENABLE_ROOT_USER, MXS_MODULE_PARAM_BOOL, "false"},
    {CN_MAX_RETRY_INTERVAL, MXS_MODULE_PARAM_COUNT, "3600"},
    {CN_MAX_CONNECTIONS, MXS_MODULE_PARAM_COUNT, "0"},
    {CN_CONNECTION_TIMEOUT, MXS_MODULE_PARAM_COUNT, "0"},
    {CN_AUTH_ALL_SERVERS, MXS_MODULE_PARAM_BOOL, "false"},
    {CN_STRIP_DB_ESC, MXS_MODULE_PARAM_BOOL, "true"},
    {CN_LOCALHOST_MATCH_WILDCARD_HOST, MXS_MODULE_PARAM_BOOL, "true"},
    {CN_VERSION_STRING, MXS_MODULE_PARAM_STRING, DEFAULT_VERSION_STRING},
    {CN_FILTERS, MXS_MODULE_PARAM_STRING},
    {CN_WEIGHTBY, MXS_MODULE_PARAM_STRING},
    {CN_LOG_AUTH_WARNINGS, MXS_MODULE_PARAM_BOOL, "true"},
    {CN_RETRY_ON_FAILURE, MXS_MODULE_PARAM_BOOL, "true"},
    {CN_SESSION_TRACK_TRX_STATE, MXS_MODULE_PARAM_BOOL, "false"},
    {NULL}
};

const MXS_MODULE_PARAM config_listener_params[] =
{
    {CN_TYPE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_SERVICE, MXS_MODULE_PARAM_SERVICE, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_PROTOCOL, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_PORT, MXS_MODULE_PARAM_COUNT}, // Either port or socket, checked when created
    {CN_SOCKET, MXS_MODULE_PARAM_STRING},
    {CN_AUTHENTICATOR_OPTIONS, MXS_MODULE_PARAM_STRING},
    {CN_ADDRESS, MXS_MODULE_PARAM_STRING, "::"},
    {CN_AUTHENTICATOR, MXS_MODULE_PARAM_STRING},
    {CN_SSL, MXS_MODULE_PARAM_ENUM, "false", MXS_MODULE_OPT_ENUM_UNIQUE, ssl_values},
    {CN_SSL_CERT, MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
    {CN_SSL_KEY, MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
    {CN_SSL_CA_CERT, MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
    {CN_SSL_VERSION, MXS_MODULE_PARAM_ENUM, "MAX", MXS_MODULE_OPT_ENUM_UNIQUE, ssl_version_values},
    {CN_SSL_CERT_VERIFY_DEPTH, MXS_MODULE_PARAM_COUNT, "9"},
    {CN_SSL_VERIFY_PEER_CERTIFICATE, MXS_MODULE_PARAM_BOOL, "true"},
    {NULL}
};

const MXS_MODULE_PARAM config_monitor_params[] =
{
    {CN_TYPE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_MODULE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},

    {CN_USER, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_PASSWORD, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},

    {CN_SERVERS, MXS_MODULE_PARAM_STRING},
    {CN_MONITOR_INTERVAL, MXS_MODULE_PARAM_COUNT, "2000"},
    {CN_BACKEND_CONNECT_TIMEOUT, MXS_MODULE_PARAM_COUNT, "3"},
    {CN_BACKEND_READ_TIMEOUT, MXS_MODULE_PARAM_COUNT, "1"},
    {CN_BACKEND_WRITE_TIMEOUT, MXS_MODULE_PARAM_COUNT, "2"},
    {CN_BACKEND_CONNECT_ATTEMPTS, MXS_MODULE_PARAM_COUNT, "1"},

    {CN_JOURNAL_MAX_AGE, MXS_MODULE_PARAM_COUNT, "28800"},
    {CN_DISK_SPACE_THRESHOLD, MXS_MODULE_PARAM_STRING},
    {CN_DISK_SPACE_CHECK_INTERVAL, MXS_MODULE_PARAM_COUNT, "0"},

    {CN_SCRIPT, MXS_MODULE_PARAM_STRING}, // Cannot be a path type as the script may have parameters
    {CN_SCRIPT_TIMEOUT, MXS_MODULE_PARAM_COUNT, "90"},
    {
        CN_EVENTS,
        MXS_MODULE_PARAM_ENUM,
        mxs_monitor_event_default_enum.name,
        MXS_MODULE_OPT_NONE,
        mxs_monitor_event_enum_values
    },
    {NULL}
};

const MXS_MODULE_PARAM config_filter_params[] =
{
    {CN_TYPE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_MODULE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {NULL}
};

const MXS_MODULE_PARAM config_server_params[] =
{
    {CN_TYPE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_ADDRESS, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_PROTOCOL, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
    {CN_PORT, MXS_MODULE_PARAM_COUNT, "3306"},
    {CN_AUTHENTICATOR, MXS_MODULE_PARAM_STRING},
    {CN_MONITORUSER, MXS_MODULE_PARAM_STRING},
    {CN_MONITORPW, MXS_MODULE_PARAM_STRING},
    {CN_PERSISTPOOLMAX, MXS_MODULE_PARAM_COUNT, "0"},
    {CN_PERSISTMAXTIME, MXS_MODULE_PARAM_COUNT, "0"},
    {CN_PROXY_PROTOCOL, MXS_MODULE_PARAM_BOOL, "false"},
    {CN_SSL, MXS_MODULE_PARAM_ENUM, "false", MXS_MODULE_OPT_ENUM_UNIQUE, ssl_values},
    {CN_SSL_CERT, MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
    {CN_SSL_KEY, MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
    {CN_SSL_CA_CERT, MXS_MODULE_PARAM_PATH, NULL, MXS_MODULE_OPT_PATH_R_OK},
    {CN_SSL_VERSION, MXS_MODULE_PARAM_ENUM, "MAX", MXS_MODULE_OPT_ENUM_UNIQUE, ssl_version_values},
    {CN_SSL_CERT_VERIFY_DEPTH, MXS_MODULE_PARAM_COUNT, "9"},
    {CN_SSL_VERIFY_PEER_CERTIFICATE, MXS_MODULE_PARAM_BOOL, "true"},
    {NULL}
};

/*
 * This is currently only used in handle_global_item() to verify that
 * all global configuration item names are valid.
 */
const char *config_pre_parse_global_params[] =
{
    CN_LOGDIR,
    CN_LIBDIR,
    CN_PIDDIR,
    CN_DATADIR,
    CN_CACHEDIR,
    CN_LANGUAGE,
    CN_EXECDIR,
    CN_CONNECTOR_PLUGINDIR,
    CN_PERSISTDIR,
    CN_MODULE_CONFIGDIR,
    CN_SYSLOG,
    CN_MAXLOG,
    CN_LOG_AUGMENTATION,
    CN_LOG_TO_SHM,
    NULL
};

const char *deprecated_server_params[] =
{
    CN_AUTHENTICATOR_OPTIONS,
    NULL
};

void config_init()
{
    config_context.object = (char*)"";
    config_context.next = NULL;
}

void config_finish()
{
    config_context_free(config_context.next);
}

/**
 * Initialize the context object used for tracking duplicate sections.
 *
 * @param context The context object to be initialized.
 *
 * @return True, if the object could be initialized.
 */
static bool duplicate_context_init(DUPLICATE_CONTEXT* context)
{
    bool rv = false;

    const int table_size = 10;
    std::set<std::string> *sections = new (std::nothrow) std::set<std::string>;
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR) "^\\s*\\[(.+)\\]\\s*$", PCRE2_ZERO_TERMINATED,
                                   0, &errcode, &erroffset, NULL);
    pcre2_match_data *mdata = NULL;

    if (sections && re && (mdata = pcre2_match_data_create_from_pattern(re, NULL)))
    {
        context->sections = sections;
        context->re = re;
        context->mdata = mdata;
        rv = true;
    }
    else
    {
        pcre2_match_data_free(mdata);
        pcre2_code_free(re);
        delete sections;
    }

    return rv;
}

/**
 * Finalize the context object used for tracking duplicate sections.
 *
 * @param context The context object to be initialized.
 */
static void duplicate_context_finish(DUPLICATE_CONTEXT* context)
{
    pcre2_match_data_free(context->mdata);
    pcre2_code_free(context->re);
    delete context->sections;

    context->mdata = NULL;
    context->re = NULL;
    context->sections = NULL;
}


/**
 * Remove extra commas and whitespace from a string. This string is interpreted
 * as a list of string values separated by commas.
 * @param strptr String to clean
 * @return pointer to a new string or NULL if an error occurred
 */
char* config_clean_string_list(const char* str)
{
    size_t destsize = strlen(str) + 1;
    char *dest = (char*)MXS_MALLOC(destsize);

    if (dest)
    {
        pcre2_code* re;
        pcre2_match_data* data;
        int re_err;
        size_t err_offset;

        if ((re = pcre2_compile((PCRE2_SPTR) "[[:space:],]*([^,]*[^[:space:],])[[:space:],]*",
                                PCRE2_ZERO_TERMINATED, 0, &re_err, &err_offset, NULL)) == NULL ||
            (data = pcre2_match_data_create_from_pattern(re, NULL)) == NULL)
        {
            PCRE2_UCHAR errbuf[MXS_STRERROR_BUFLEN];
            pcre2_get_error_message(re_err, errbuf, sizeof(errbuf));
            MXS_ERROR("[%s] Regular expression compilation failed at %d: %s",
                      __FUNCTION__, (int)err_offset, errbuf);
            pcre2_code_free(re);
            MXS_FREE(dest);
            return NULL;
        }

        const char *replace = "$1,";
        int rval = 0;
        size_t destsize_tmp = destsize;
        while ((rval = pcre2_substitute(re, (PCRE2_SPTR) str, PCRE2_ZERO_TERMINATED, 0,
                                        PCRE2_SUBSTITUTE_GLOBAL, data, NULL,
                                        (PCRE2_SPTR) replace, PCRE2_ZERO_TERMINATED,
                                        (PCRE2_UCHAR*) dest, &destsize_tmp)) == PCRE2_ERROR_NOMEMORY)
        {
            destsize_tmp = 2 * destsize;
            char* tmp = (char*)MXS_REALLOC(dest, destsize_tmp);
            if (tmp == NULL)
            {
                MXS_FREE(dest);
                dest = NULL;
                break;
            }
            dest = tmp;
            destsize = destsize_tmp;
        }

        /** Remove the trailing comma */
        if (dest && dest[strlen(dest) - 1] == ',')
        {
            dest[strlen(dest) - 1] = '\0';
        }

        pcre2_code_free(re);
        pcre2_match_data_free(data);
    }

    return dest;
}

CONFIG_CONTEXT* config_context_create(const char *section)
{
    CONFIG_CONTEXT* ctx = (CONFIG_CONTEXT *)MXS_MALLOC(sizeof(CONFIG_CONTEXT));
    if (ctx)
    {
        ctx->object = MXS_STRDUP_A(section);
        ctx->was_persisted = is_persisted_config;
        ctx->parameters = NULL;
        ctx->next = NULL;
    }

    return ctx;
}

/** A set that holds all the section names that contain whitespace */
static std::set<string> warned_whitespace;

static void fix_section_name(char *section)
{
    for (char* s = section; *s; s++)
    {
        if (isspace(*s))
        {
            if (warned_whitespace.find(section) == warned_whitespace.end())
            {
                warned_whitespace.insert(section);
                MXS_WARNING("Whitespace in object names is deprecated, "
                            "converting to hyphens: %s", section);
            }
            break;
        }
    }

    fix_object_name(section);
}

void fix_object_name(char *name)
{
    squeeze_whitespace(name);
    trim(name);
    replace_whitespace(name);
}

void fix_object_name(std::string& name)
{
    char buf[name.size() + 1];
    strcpy(buf, name.c_str());
    fix_object_name(buf);
    name.assign(buf);
}

static bool is_empty_string(const char* str)
{
    for (const char* p = str; *p; p++)
    {
        if (!isspace(*p))
        {
            return false;
        }
    }

    return true;
}

static bool is_maxscale_section(const char* section)
{
    return strcasecmp(section, CN_GATEWAY) == 0 || strcasecmp(section, CN_MAXSCALE) == 0;
}

static bool is_root_config_file = true;

static int ini_global_handler(void *userdata, const char *section, const char *name, const char *value)
{
    return is_maxscale_section(section) ? handle_global_item(name, value) : 1;
}

/**
 * Config item handler for the ini file reader
 *
 * @param userdata      The config context element
 * @param section       The config file section
 * @param name          The Parameter name
 * @param value         The Parameter value
 * @return zero on error
 */
static int ini_handler(void *userdata, const char *section, const char *name, const char *value)
{
    CONFIG_CONTEXT *cntxt = (CONFIG_CONTEXT *)userdata;
    CONFIG_CONTEXT *ptr = cntxt;

    const std::set<std::string> legacy_parameters{"passwd"};

    if (is_persisted_config && legacy_parameters.count(name))
    {
        /**
         * Ignore legacy parameters in persisted configurations. Needs to be
         * done to make upgrades from pre-2.3 versions work.
         */
        return 1;
    }

    if (is_empty_string(value))
    {
        if (is_persisted_config)
        {
            /**
             * Found old-style persisted configuration. These will be automatically
             * upgraded on the next modification so we can safely ignore it.
             */
            return 1;
        }
        else
        {
            MXS_ERROR("Empty value given to parameter '%s'", name);
            return 0;
        }
    }

    if (config_get_global_options()->substitute_variables)
    {
        if (*value == '$')
        {
            char* env_value = getenv(value + 1);

            if (!env_value)
            {
                MXS_ERROR("The environment variable %s, used as value for parameter %s "
                          "in section %s, does not exist.", value + 1, name, section);
                return 0;
            }

            value = env_value;
        }
    }

    if (strlen(section) == 0)
    {
        MXS_ERROR("Parameter '%s=%s' declared outside a section.", name, value);
        return 0;
    }

    char fixed_section[strlen(section) + 1];
    strcpy(fixed_section, section);
    fix_section_name(fixed_section);

    /*
     * If we already have some parameters for the object
     * add the parameters to that object. If not create
     * a new object.
     */
    while (ptr && strcmp(ptr->object, fixed_section) != 0)
    {
        ptr = ptr->next;
    }

    if (!ptr)
    {
        if ((ptr = config_context_create(fixed_section)) == NULL)
        {
            return 0;
        }

        ptr->next = cntxt->next;
        cntxt->next = ptr;
    }

    if (config_get_param(ptr->parameters, name))
    {
        /** The values in the persisted configurations are updated versions of
         * the ones in the main configuration file.  */
        if (is_persisted_config)
        {
            if (!config_replace_param(ptr, name, value))
            {
                return 0;
            }
        }
        /** Multi-line parameter */
        else if (!config_append_param(ptr, name, value))
        {
            return 0;
        }
    }
    else if (!config_add_param(ptr, name, value))
    {
        return 0;
    }

    if (is_maxscale_section(section))
    {
        if (!is_root_config_file && !is_persisted_config)
        {
            MXS_ERROR("The [maxscale] section must only be defined in the root configuration file.");
            return 0;
        }
    }

    return 1;
}

static void log_config_error(const char* file, int rval)
{
    char errorbuffer[1024 + 1];

    if (rval > 0)
    {
        snprintf(errorbuffer, sizeof(errorbuffer),
                 "Failed to parse configuration file %s. Error on line %d.", file, rval);
    }
    else if (rval == -1)
    {
        snprintf(errorbuffer, sizeof(errorbuffer),
                 "Failed to parse configuration file %s. Could not open file.", file);
    }
    else
    {
        snprintf(errorbuffer, sizeof(errorbuffer),
                 "Failed to parse configuration file %s. Memory allocation failed.", file);
    }

    MXS_ERROR("%s", errorbuffer);
}

/**
 * Load single configuration file.
 *
 * @param file     The file to load.
 * @param dcontext The context object used when tracking duplicate sections.
 * @param ccontext The context object used when parsing.
 *
 * @return True if the file could be parsed, false otherwise.
 */
static bool config_load_single_file(const char* file,
                                    DUPLICATE_CONTEXT* dcontext,
                                    CONFIG_CONTEXT* ccontext)
{
    int rval = -1;

    // With multiple configuration files being loaded, we need to log the file
    // currently being loaded so that the context is clear in case of errors.
    MXS_NOTICE("Loading %s.", file);

    if (!config_has_duplicate_sections(file, dcontext))
    {
        if ((rval = ini_parse(file, ini_handler, ccontext)) != 0)
        {
            log_config_error(file, rval);
        }
    }

    /* Check this after reading config is finished */
    if ((gateway.writeq_high_water || gateway.writeq_low_water) &&
        gateway.writeq_high_water <= gateway.writeq_low_water)
    {
        rval = -1;
        MXS_ERROR("Invaild configuration, writeq_high_water should be greater than writeq_low_water");
    }

    return rval == 0;
}

/**
 * The current parsing contexts must be managed explicitly since the ftw callback
 * can not have user data.
 */
static CONFIG_CONTEXT *current_ccontext;
static DUPLICATE_CONTEXT *current_dcontext;

/**
 * The nftw callback.
 *
 * @see man ftw
 */
int config_cb(const char* fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rval = 0;

    if (typeflag == FTW_SL) // A symbolic link; let's see what it points to.
    {
        struct stat sb;

        if (stat(fpath, &sb) == 0)
        {
            int file_type = (sb.st_mode & S_IFMT);

            switch (file_type)
            {
            case S_IFREG:
                // Points to a file; we'll handle that regardless of where the file resides.
                typeflag = FTW_F;
                break;

            case S_IFDIR:
                // Points to a directory; we'll ignore that.
                MXS_WARNING("Symbolic link %s in configuration directory points to a "
                            "directory; it will be ignored.", fpath);
                break;

            default:
                // Points to something else; we'll silently ignore.
                ;
            }
        }
        else
        {
            MXS_WARNING("Could not get information about the symbolic link %s; "
                        "it will be ignored.", fpath);
        }
    }

    if (typeflag == FTW_F) // We are only interested in files,
    {
        const char* filename = fpath + ftwbuf->base;
        const char* dot = strrchr(filename, '.');

        if (dot && *filename != '.') // that have a suffix and are not hidden,
        {
            const char* suffix = dot + 1;

            if (strcmp(suffix, "cnf") == 0) // that is ".cnf".
            {
                mxb_assert(current_dcontext);
                mxb_assert(current_ccontext);

                if (!config_load_single_file(fpath, current_dcontext, current_ccontext))
                {
                    rval = -1;
                }
            }
        }
    }

    return rval;
}

/**
 * Loads all configuration files in a directory hierarchy.
 *
 * Only files with the suffix ".cnf" are considered to be configuration files.
 *
 * @param dir      The directory.
 * @param dcontext The duplicate section context.
 * @param ccontext The configuration context.
 *
 * @return True, if all configuration files in the directory hierarchy could be loaded,
 *         otherwise false.
 */
static bool config_load_dir(const char *dir, DUPLICATE_CONTEXT *dcontext, CONFIG_CONTEXT *ccontext)
{
    // Since there is no way to pass userdata to the callback, we need to store
    // the current context into a static variable. Consequently, we need lock.
    // Should not matter since config_load() is called once at startup.
    static SPINLOCK lock = SPINLOCK_INIT;

    int nopenfd = 5; // Maximum concurrently opened directory descriptors

    spinlock_acquire(&lock);
    current_dcontext = dcontext;
    current_ccontext = ccontext;
    int rv = nftw(dir, config_cb, nopenfd, FTW_PHYS);
    current_ccontext = NULL;
    current_dcontext = NULL;
    spinlock_release(&lock);

    return rv == 0;
}

/**
 * Check if a directory exists
 *
 * This function also logs warnings if the directory cannot be accessed or if
 * the file is not a directory.
 * @param dir Directory to check
 * @return True if the file is an existing directory
 */
static bool is_directory(const char *dir)
{
    bool rval = false;
    struct stat st;
    if (stat(dir, &st) == -1)
    {
        if (errno == ENOENT)
        {
            MXS_NOTICE("%s does not exist, not reading.", dir);
        }
        else
        {
            MXS_WARNING("Could not access %s, not reading: %s",
                        dir, mxs_strerror(errno));
        }
    }
    else
    {
        if (S_ISDIR(st.st_mode))
        {
            rval = true;
        }
        else
        {
            MXS_WARNING("%s exists, but it is not a directory. Ignoring.", dir);
        }
    }

    return rval;
}

/**
 * @brief Check if a directory contains .cnf files
 *
 * @param path Path to a directory
 * @return True if the directory contained one or more .cnf files
 */
static bool contains_cnf_files(const char *path)
{
    bool rval = false;
    glob_t matches;
    const char suffix[] = "/*.cnf";
    char pattern[strlen(path) + sizeof(suffix)];

    strcpy(pattern, path);
    strcat(pattern, suffix);
    int rc = glob(pattern, GLOB_NOSORT, NULL, &matches);

    switch (rc)
    {
    case 0:
        rval = true;
        break;

    case GLOB_NOSPACE:
        MXS_OOM();
        break;

    case GLOB_ABORTED:
        MXS_ERROR("Failed to read directory '%s'", path);
        break;

    default:
        mxb_assert(rc == GLOB_NOMATCH);
        break;
    }

    globfree(&matches);

    return rval;
}

bool export_config_file(const char* filename)
{
    bool rval = true;
    std::vector<CONFIG_CONTEXT*> contexts;

    // The config objects are stored in reverse order so first convert it back
    // to the correct order
    for (CONFIG_CONTEXT* ctx = config_context.next; ctx; ctx = ctx->next)
    {
        contexts.push_back(ctx);
    }

    std::ofstream file(filename);

    if (file)
    {
        time_t now = time(NULL);
        file << "# Generated by MaxScale " << MAXSCALE_VERSION << '\n';
        file << "# Documentation: https://mariadb.com/kb/en/mariadb-enterprise/maxscale/ \n\n";

        for (auto it = contexts.rbegin(); it != contexts.rend(); it++)
        {
            CONFIG_CONTEXT* ctx = *it;

            file << '[' << ctx->object << "]\n";

            // Parameters are also stored in reverse order
            std::vector<MXS_CONFIG_PARAMETER*> params;

            for (MXS_CONFIG_PARAMETER* p = ctx->parameters; p; p = p->next)
            {
                params.push_back(p);
            }

            for (auto pit = params.rbegin(); pit != params.rend(); pit++)
            {
                MXS_CONFIG_PARAMETER* p = *pit;
                file << p->name << '=' << p->value << '\n';
            }

            file << '\n';
        }
    }
    else
    {
        MXS_ERROR("Failed to open configuration export file '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
        rval = false;
    }

    return rval;
}

/**
 * @brief Load the specified configuration file for MaxScale
 *
 * This function will parse the configuration file, check for duplicate sections,
 * validate the module parameters and finally turn it into a set of objects.
 *
 * @param filename        The filename of the configuration file
 * @param process_config  The function using which the successfully loaded
 *                        configuration should be processed.
 *
 * @return True on success, false on fatal error
 */
static bool
config_load_and_process(const char* filename, bool (*process_config)(CONFIG_CONTEXT*))
{
    bool rval = false;
    DUPLICATE_CONTEXT dcontext;

    if (duplicate_context_init(&dcontext))
    {
        if (config_load_single_file(filename, &dcontext, &config_context))
        {
            is_root_config_file = false;
            const char DIR_SUFFIX[] = ".d";

            char dir[strlen(filename) + sizeof(DIR_SUFFIX)];
            strcpy(dir, filename);
            strcat(dir, DIR_SUFFIX);

            rval = true;

            if (is_directory(dir))
            {
                rval = config_load_dir(dir, &dcontext, &config_context);
            }

            /** Create the persisted configuration directory if it doesn't exist */
            const char* persist_cnf = get_config_persistdir();
            mxs_mkdir_all(persist_cnf, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

            if (is_directory(persist_cnf) && contains_cnf_files(persist_cnf))
            {
                /**
                 * Set the global flag that we are processing a persisted configuration.
                 * This will tell the modules whether it is OK to completely overwrite
                 * the persisted configuration when changes are made.
                 *
                 * TODO: Figure out a cleaner way to do this
                 */
                is_persisted_config = true;

                MXS_NOTICE("Loading generated configuration files from '%s'", persist_cnf);
                DUPLICATE_CONTEXT p_dcontext;
                /**
                 * We need to initialize a second duplicate context for the
                 * generated configuration files as the monitors and services will
                 * have duplicate sections. The duplicate sections are used to
                 * store changes to the list of servers the services and monitors
                 * use, and thus should not be treated as errors.
                 */
                if (duplicate_context_init(&p_dcontext))
                {
                    rval = config_load_dir(persist_cnf, &p_dcontext, &config_context);
                    duplicate_context_finish(&p_dcontext);
                }
                else
                {
                    rval = false;
                }
                is_persisted_config = false;
            }

            if (rval)
            {
                if (!check_config_objects(config_context.next) || !process_config(config_context.next))
                {
                    rval = false;
                    if (contains_cnf_files(persist_cnf))
                    {
                        MXS_WARNING("One or more generated configurations were found at '%s'. "
                                    "If the error relates to any of the files located there, "
                                    "remove the offending configurations from this directory.",
                                    persist_cnf);
                    }
                }
            }
        }

        duplicate_context_finish(&dcontext);
    }
    return rval;
}

bool config_load_global(const char *filename)
{
    int rval;

    if ((rval = ini_parse(filename, ini_global_handler, NULL)) != 0)
    {
        log_config_error(filename, rval);
    }
    else
    {
        // Do some post-processing for auto-sized default variables
        if (gateway.qc_cache_properties.max_size == -1)
        {
            int64_t mem_per_thr = get_total_memory() * 0.4 / gateway.n_threads;
            mxb_assert(mem_per_thr >= 0);
            gateway.qc_cache_properties.max_size = mem_per_thr;

            if (mem_per_thr == 0)
            {
                MXS_WARNING("Could not auto-detect total system memory for the query classifier "
                            "cache. Manually define `%s` to enable it.", CN_QUERY_CLASSIFIER_CACHE_SIZE);
            }
        }
    }

    return rval == 0;
}

/**
 * @brief Load the configuration file for the MaxScale
 *
 * @param filename The filename of the configuration file
 * @return True on success, false on fatal error
 */
bool
config_load(const char *filename)
{
    mxb_assert(!config_file);

    config_file = filename;
    bool rval = config_load_and_process(filename, process_config_context);

    return rval;
}

bool valid_object_type(std::string type)
{
    std::set<std::string> types{CN_SERVICE, CN_LISTENER, CN_SERVER, CN_MONITOR, CN_FILTER};
    return types.count(type);
}

/**
 * Get the value of a config parameter
 *
 * @param params        The linked list of config parameters
 * @param name          The parameter to return
 * @return the parameter value or NULL if not found
 */
static char* config_get_value(MXS_CONFIG_PARAMETER *params, const char *name)
{
    while (params)
    {
        if (!strcmp(params->name, name))
        {
            return params->value;
        }

        params = params->next;
    }
    return NULL;
}

const MXS_MODULE* get_module(CONFIG_CONTEXT* obj, const char* param_name, const char* module_type)
{
    const char* module = config_get_value(obj->parameters, param_name);
    return module ? get_module(module, module_type) : NULL;
}

std::pair<const MXS_MODULE_PARAM*, const MXS_MODULE*> get_module_details(const CONFIG_CONTEXT* obj)
{
    std::string type = config_get_string(obj->parameters, CN_TYPE);

    if (type == CN_SERVICE)
    {
        const char* name = config_get_string(obj->parameters, CN_ROUTER);
        return {config_service_params, get_module(name, MODULE_ROUTER)};
    }
    else if (type == CN_LISTENER)
    {
        const char* name = config_get_string(obj->parameters, CN_PROTOCOL);
        return {config_listener_params, get_module(name, MODULE_PROTOCOL)};
    }
    else if (type == CN_SERVER)
    {
        const char* name = config_get_string(obj->parameters, CN_PROTOCOL);
        return {config_server_params, get_module(name, MODULE_PROTOCOL)};
    }
    else if (type == CN_MONITOR)
    {
        const char* name = config_get_string(obj->parameters, CN_MODULE);
        return {config_monitor_params, get_module(name, MODULE_MONITOR)};
    }
    else if (type == CN_FILTER)
    {
        const char* name = config_get_string(obj->parameters, CN_MODULE);
        return {config_filter_params, get_module(name, MODULE_FILTER)};
    }

    mxb_assert(!true);
    return {nullptr, nullptr};
}

CONFIG_CONTEXT* name_to_object(const std::vector<CONFIG_CONTEXT*>& objects,
                               const CONFIG_CONTEXT* obj, std::string name)
{
    CONFIG_CONTEXT* rval = nullptr;

    fix_object_name(name);

    auto equal_name = [&](CONFIG_CONTEXT * c)
    {
        std::string s = c->object;
        fix_object_name(s);
        return s == name;
    };

    auto it = std::find_if(objects.begin(), objects.end(), equal_name);

    if (it == objects.end())
    {
        MXS_ERROR("Could not find object '%s' that '%s' depends on. "
                  "Check that the configuration object exists.",
                  name.c_str(), obj->object);
    }
    else
    {
        rval = *it;
    }

    return rval;
}

std::unordered_set<CONFIG_CONTEXT*> get_dependencies(const std::vector<CONFIG_CONTEXT*>& objects,
                                                     const CONFIG_CONTEXT* obj)
{
    std::unordered_set<CONFIG_CONTEXT*> rval;
    const MXS_MODULE_PARAM* params;
    const MXS_MODULE* module;
    std::tie(params, module) = get_module_details(obj);

    // Astyle really hates this style. Could be worked around with --keep-one-line-blocks
    // but it would keep all one line blocks intact.
    for (const auto& p :
         {
             params, module->parameters
         })
    {
        for (int i = 0; p[i].name; i++)
        {
            if (config_get_value(obj->parameters, p[i].name))
            {
                if (p[i].type == MXS_MODULE_PARAM_SERVICE ||
                    p[i].type == MXS_MODULE_PARAM_SERVER)
                {
                    std::string v = config_get_string(obj->parameters, p[i].name);
                    rval.insert(name_to_object(objects, obj, v));
                }
            }
        }
    }

    std::string type = config_get_string(obj->parameters, CN_TYPE);

    if (type == CN_SERVICE && config_get_value(obj->parameters, CN_FILTERS))
    {
        for (std::string name : mxs::strtok(config_get_string(obj->parameters, CN_FILTERS), "|"))
        {
            rval.insert(name_to_object(objects, obj, name));
        }
    }

    if ((type == CN_MONITOR || type == CN_SERVICE) && config_get_value(obj->parameters, CN_SERVERS))
    {
        for (std::string name : mxs::strtok(config_get_string(obj->parameters, CN_SERVERS), ","))
        {
            rval.insert(name_to_object(objects, obj, name));
        }
    }

    return rval;
}

namespace
{

// Represents a node in a graph
template <class T>
struct Node
{
    static const int NOT_VISITED = 0;

    T value;
    int index;
    int lowlink;
    bool on_stack;

    Node(T value) :
        value(value),
        index(NOT_VISITED),
        lowlink(NOT_VISITED),
        on_stack(false)
    {
    }
};

template <class T> using Container = std::unordered_map<T, std::unordered_set<T>>;
template <class T> using Groups = std::vector<std::vector<T>>;
template <class T> using Graph = std::unordered_multimap<Node<T>*, Node<T>*>;

/**
 * Calculate strongly connected components (i.e. cycles) of a graph
 *
 * @see https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
 *
 * @param graph An std::unordered_multimap where the keys represent nodes and
 *              the set of values for that key as the edges from that node
 *
 * @return A list of groups where each group is an ordered list of values
 */
template <class T>
Groups<T> get_graph_cycles(Container<T> graph)
{
    using namespace std::placeholders;

    std::vector<Node<T>> nodes;

    auto find_node = [&](T target, const Node<T>& n)
    {
        return n.value == target;
    };

    // Iterate over all values and place unique values in the vector.
    for (auto&& a : graph)
    {
        nodes.emplace_back(a.first);
    }

    Graph<T> node_graph;

    for (auto&& a : graph)
    {
        auto first = std::find_if(nodes.begin(), nodes.end(), std::bind(find_node, a.first, _1));

        for (auto&& b : a.second)
        {
            auto second = std::find_if(nodes.begin(), nodes.end(), std::bind(find_node, b, _1));
            node_graph.emplace(&(*first), &(*second));
        }
    }

    std::vector<Node<T>*> stack;
    Groups<T> groups;

    std::function<void (Node<T>*)> visit_node = [&](Node<T>* n)
    {
        static int s_index = 1;
        n->index = s_index++;
        n->lowlink = n->index;
        stack.push_back(n);
        n->on_stack = true;
        auto range = node_graph.equal_range(n);

        for (auto it = range.first; it != range.second; it++)
        {
            Node<T>* s = it->second;

            if (s->index == Node<T>::NOT_VISITED)
            {
                visit_node(s);
                n->lowlink = std::min(n->lowlink, s->lowlink);
            }
            else if (s->on_stack)
            {
                n->lowlink = std::min(n->lowlink, s->index);
            }
        }

        if (n->index == n->lowlink)
        {
            // Start a new group
            groups.emplace_back();

            Node<T>* c;

            do
            {
                c = stack.back();
                stack.pop_back();
                c->on_stack = false;
                groups.back().push_back(c->value);
            }
            while (c != n);
        }
    };

    for (auto n = nodes.begin(); n != nodes.end(); n++)
    {
        if (n->index == Node<T>::NOT_VISITED)
        {
            visit_node((Node<T>*) & (*n));
        }
    }

    return groups;
}

}

/**
 * Resolve dependencies in the configuration and validate them
 *
 * @param objects List of objects, sorted so that dependencies are constructed first
 *
 * @return True if the configuration has bad dependencies
 */
bool resolve_dependencies(std::vector<CONFIG_CONTEXT*>& objects)
{
    int errors = 0;
    std::unordered_map<CONFIG_CONTEXT*, std::unordered_set < CONFIG_CONTEXT*>> g;

    for (const auto& obj : objects)
    {
        auto deps = get_dependencies(objects, obj);

        if (deps.count(nullptr))
        {
            // a missing reference, reported in get_dependencies
            errors++;
        }
        else
        {
            g.insert(std::make_pair(obj, deps));
        }
    }

    if (errors == 0)
    {
        std::vector<CONFIG_CONTEXT*> result;

        for (const auto& group : get_graph_cycles<CONFIG_CONTEXT*>(g))
        {
            if (group.size() > 1)
            {
                auto join = [](std::string total, CONFIG_CONTEXT * c)
                {
                    return total + " -> " + c->object;
                };

                std::string first = group[0]->object;
                std::string str_group = std::accumulate(std::next(group.begin()), group.end(), first, join);
                str_group += " -> " + first;
                MXS_ERROR("A circular dependency chain was found in the configuration: %s", str_group.c_str());
                errors++;
            }
            else
            {
                mxb_assert(!group.empty());
                /** Due to the algorithm that was used, the strongly connected
                 * components are always identified before the nodes that depend
                 * on them. This means that the result is sorted at the same
                 * time the circular dependencies are resolved. */
                result.push_back(group[0]);
            }
        }

        // The end result should contain the same set of nodes we started with
        mxb_assert(std::set<CONFIG_CONTEXT*>(result.begin(), result.end()) ==
                   std::set<CONFIG_CONTEXT*>(objects.begin(), objects.end()));

        objects = std::move(result);
    }

    return errors > 0;
}

/**
 * @brief Process a configuration context and turn it into the set of objects
 *
 * @param context The parsed configuration context
 * @return False on fatal error, true on success
 */
static bool
process_config_context(CONFIG_CONTEXT *context)
{
    std::vector<CONFIG_CONTEXT*> objects;

    for (CONFIG_CONTEXT* obj = context; obj; obj = obj->next)
    {
        if (!is_maxscale_section(obj->object))
        {
            objects.push_back(obj);
        }
    }

    int error_count = 0;

    /**
     * Build the servers first to keep them in configuration file order. As
     * servers can't have references, this is safe to do as the first step.
     */
    for (CONFIG_CONTEXT* obj : objects)
    {
        std::string type = config_get_string(obj->parameters, CN_TYPE);
        mxb_assert(!type.empty());

        if (type == CN_SERVER)
        {
            error_count += create_new_server(obj);
        }
    }

    // Resolve any remaining dependencies between the objects
    if (resolve_dependencies(objects) || error_count)
    {
        return false;
    }

    std::set<std::string> monitored_servers;

    /**
     * Process the data and create the services defined in the data.
     */
    for (CONFIG_CONTEXT* obj : objects)
    {
        std::string type = config_get_string(obj->parameters, CN_TYPE);
        mxb_assert(!type.empty());

        if (type == CN_SERVICE)
        {
            error_count += create_new_service(obj);
        }
        else if (type == CN_FILTER)
        {
            error_count += create_new_filter(obj);
        }
        else if (type == CN_LISTENER)
        {
            error_count += create_new_listener(obj);
        }
        else if (type == CN_MONITOR)
        {
            error_count += create_new_monitor(obj, monitored_servers);
        }

        if (error_count)
        {
            /**
             * We need to stop creating objects after the first error since
             * any objects that depend on the object that failed would fail in
             * a very confusing manner.
             */
            break;
        }
    }

    if (error_count)
    {
        MXS_ERROR("%d errors were encountered while processing the configuration "
                  "file '%s'.", error_count, config_file);
    }

    return error_count == 0;
}

// DEPRECATE: In 2.1 complain but accept if "passwd" is provided, in 2.2
// DEPRECATE: drop support for "passwd".
/**
 * Get the value of the password parameter
 *
 * The words looked for are "password" and "passwd".
 *
 * @param params        The linked list of config parameters
 * @return the parameter value or NULL if not found
 */
static char *
config_get_password(MXS_CONFIG_PARAMETER *params)
{
    char *password = config_get_value(params, CN_PASSWORD);
    char *passwd = config_get_value(params, "passwd");

    if (password && passwd)
    {
        MXS_WARNING("Both 'password' and 'passwd' specified. Using value of 'password'.");
    }

    return passwd ? passwd : password;
}

/**
 * Get the value of a config parameter as a string
 *
 * @param params        The linked list of config parameters
 * @param name          The parameter to return
 * @return the parameter value or null string if not found
 */
static const char* config_get_value_string(const MXS_CONFIG_PARAMETER *params, const char *name)
{
    while (params)
    {
        if (!strcmp(params->name, name))
        {
            return (const char *)params->value;
        }

        params = params->next;
    }
    return "";
}

MXS_CONFIG_PARAMETER* config_get_param(MXS_CONFIG_PARAMETER* params, const char* name)
{
    while (params)
    {
        if (!strcmp(params->name, name))
        {
            return params;
        }

        params = params->next;
    }
    return NULL;
}

bool config_get_bool(const MXS_CONFIG_PARAMETER *params, const char *key)
{
    const char *value = config_get_value_string(params, key);
    return *value ? config_truth_value(value) : false;
}

int config_get_integer(const MXS_CONFIG_PARAMETER *params, const char *key)
{
    const char *value = config_get_value_string(params, key);
    return *value ? strtol(value, NULL, 10) : 0;
}

uint64_t config_get_size(const MXS_CONFIG_PARAMETER *params, const char *key)
{
    const char *value = config_get_value_string(params, key);
    uint64_t intval;
    MXB_AT_DEBUG(bool rval = )get_suffixed_size(value, &intval);
    mxb_assert(rval);
    return intval;
}

const char* config_get_string(const MXS_CONFIG_PARAMETER *params, const char *key)
{
    return config_get_value_string(params, key);
}

int config_get_enum(const MXS_CONFIG_PARAMETER *params, const char *key, const MXS_ENUM_VALUE *enum_values)
{
    const char *value = config_get_value_string(params, key);
    char tmp_val[strlen(value) + 1];
    strcpy(tmp_val, value);

    int rv = 0;
    bool found = false;
    char *endptr;
    const char *delim = ", \t";
    char *tok = strtok_r(tmp_val, delim, &endptr);

    while (tok)
    {
        for (int i = 0; enum_values[i].name; i++)
        {
            if (strcmp(enum_values[i].name, tok) == 0)
            {
                found = true;
                rv |= enum_values[i].enum_value;
            }
        }
        tok = strtok_r(NULL, delim, &endptr);
    }

    return found ? rv : -1;
}

SERVICE* config_get_service(const MXS_CONFIG_PARAMETER *params, const char *key)
{
    const char *value = config_get_value_string(params, key);
    return service_find(value);
}

SERVER* config_get_server(const MXS_CONFIG_PARAMETER *params, const char *key)
{
    const char *value = config_get_value_string(params, key);
    return server_find_by_unique_name(value);
}

int config_get_server_list(const MXS_CONFIG_PARAMETER *params, const char *key,
                           SERVER*** output)
{
    const char *value = config_get_value_string(params, key);
    char **server_names = NULL;
    int found = 0;
    const int n_names = config_parse_server_list(value, &server_names);
    if (n_names > 0)
    {
        SERVER** servers;
        found = server_find_by_unique_names(server_names, n_names, &servers);
        for (int i = 0; i < n_names; i++)
        {
            MXS_FREE(server_names[i]);
        }
        MXS_FREE(server_names);

        if (found)
        {
            /* Fill in the result array */
            SERVER** result = (SERVER**)MXS_CALLOC(found, sizeof(SERVER*));
            if (result)
            {
                int res_ind = 0;
                for (int i = 0; i < n_names; i++)
                {
                    if (servers[i])
                    {
                        result[res_ind] = servers[i];
                        res_ind++;
                    }
                }
                *output = result;
                mxb_assert(found == res_ind);
            }
            MXS_FREE(servers);
        }
    }
    return found;
}

char* config_copy_string(const MXS_CONFIG_PARAMETER *params, const char *key)
{
    const char *value = config_get_value_string(params, key);

    char *rval = NULL;

    if (*value)
    {
        rval = MXS_STRDUP_A(value);
    }

    return rval;
}

pcre2_code* config_get_compiled_regex(const MXS_CONFIG_PARAMETER *params,
                                      const char *key, uint32_t options,
                                      uint32_t* output_ovec_size)
{
    const char* regex_string = config_get_string(params, key);
    pcre2_code* code = NULL;

    if (*regex_string)
    {
        uint32_t jit_available = 0;
        pcre2_config(PCRE2_CONFIG_JIT, &jit_available);
        code = compile_regex_string(regex_string, jit_available, options, output_ovec_size);
    }

    return code;
}

bool config_get_compiled_regexes(const MXS_CONFIG_PARAMETER *params,
                                 const char* keys[], int keys_size,
                                 uint32_t options, uint32_t* out_ovec_size,
                                 pcre2_code** out_codes[])
{
    bool rval = true;
    uint32_t max_ovec_size = 0;
    uint32_t ovec_size_temp = 0;
    for (int i = 0; i < keys_size; i++)
    {
        mxb_assert(out_codes[i]);
        *out_codes[i] = config_get_compiled_regex(params, keys[i], options,
                                                  &ovec_size_temp);
        if (*out_codes[i])
        {
            if (ovec_size_temp > max_ovec_size)
            {
                max_ovec_size = ovec_size_temp;
            }
        }
        /* config_get_compiled_regex() returns null also if the config setting
         * didn't exist. Check that before setting error state. */
        else if (*(config_get_value_string(params, keys[i])))
        {
            rval = false;
        }
    }
    if (out_ovec_size)
    {
        *out_ovec_size = max_ovec_size;
    }
    return rval;
}

MXS_CONFIG_PARAMETER* config_clone_param(const MXS_CONFIG_PARAMETER* param)
{
    MXS_CONFIG_PARAMETER *p2 = (MXS_CONFIG_PARAMETER*)MXS_MALLOC(sizeof(MXS_CONFIG_PARAMETER));

    if (p2)
    {
        p2->name = MXS_STRDUP_A(param->name);
        p2->value = MXS_STRDUP_A(param->value);
        p2->next = NULL;
    }

    return p2;
}

void config_free_one_param(MXS_CONFIG_PARAMETER* p1)
{
    if (p1)
    {
        MXS_FREE(p1->name);
        MXS_FREE(p1->value);
        MXS_FREE(p1);
    }
}

/**
 * Free a configuration parameter
 * @param p1 Parameter to free
 */
void config_parameter_free(MXS_CONFIG_PARAMETER* p1)
{
    while (p1)
    {
        MXS_CONFIG_PARAMETER* tmp = p1;
        p1 = p1->next;
        config_free_one_param(tmp);
    }
}

void config_context_free(CONFIG_CONTEXT *context)
{
    CONFIG_CONTEXT   *obj;

    while (context)
    {
        obj = context->next;
        config_parameter_free(context->parameters);
        MXS_FREE(context->object);
        MXS_FREE(context);
        context = obj;
    }
}

bool config_add_param(CONFIG_CONTEXT* obj, const char* key, const char* value)
{
    mxb_assert(config_get_param(obj->parameters, key) == NULL);
    bool rval = false;
    char *my_key = MXS_STRDUP(key);
    char *my_value = MXS_STRDUP(value);
    MXS_CONFIG_PARAMETER* param = (MXS_CONFIG_PARAMETER *)MXS_MALLOC(sizeof(*param));

    if (my_key && my_value && param)
    {
        param->name = my_key;
        param->value = my_value;
        param->next = obj->parameters;
        obj->parameters = param;
        rval = true;
    }
    else
    {
        MXS_FREE(my_key);
        MXS_FREE(my_value);
        MXS_FREE(param);
    }

    return rval;
}

bool config_append_param(CONFIG_CONTEXT* obj, const char* key, const char* value)
{
    MXS_CONFIG_PARAMETER *param = config_get_param(obj->parameters, key);
    mxb_assert(param);
    int paramlen = strlen(param->value) + strlen(value) + 2;
    char tmp[paramlen];
    bool rval = false;

    strcpy(tmp, param->value);
    strcat(tmp, ",");
    strcat(tmp, value);

    char *new_value = config_clean_string_list(tmp);

    if (new_value)
    {
        MXS_FREE(param->value);
        param->value = new_value;
        rval = true;
    }

    return rval;
}

bool config_replace_param(CONFIG_CONTEXT* obj, const char* key, const char* value)
{
    if (MXS_CONFIG_PARAMETER* param = config_get_param(obj->parameters, key))
    {
        MXS_FREE(param->value);
        param->value = MXS_STRDUP(value);
    }
    else
    {
        config_add_param(obj, key, value);
    }

    return true;
}

void config_remove_param(CONFIG_CONTEXT* obj, const char* name)
{
    if (obj->parameters && strcmp(obj->parameters->name, name) == 0)
    {
        MXS_CONFIG_PARAMETER* tmp = obj->parameters;
        obj->parameters = obj->parameters->next;
        config_free_one_param(tmp);
    }
    else
    {
        for (MXS_CONFIG_PARAMETER* p = obj->parameters; p && p->next; p = p->next)
        {
            if (strcmp(p->next->name, name) == 0)
            {
                MXS_CONFIG_PARAMETER* tmp = p->next;
                p->next = tmp->next;
                config_free_one_param(tmp);
                break;
            }
        }
    }
}

/**
 * Return the number of configured threads
 *
 * @return The number of threads configured in the config file
 */
int
config_threadcount()
{
    return gateway.n_threads;
}

size_t config_thread_stack_size()
{
    return gateway.thread_stack_size;
}

/**
 * Return the number of non-blocking polls to be done before a blocking poll
 * is issued.
 *
 * @return The number of blocking poll calls to make before a blocking call
 */
unsigned int
config_nbpolls()
{
    return gateway.n_nbpoll;
}

uint32_t config_writeq_high_water()
{
    return gateway.writeq_high_water;
}

uint32_t config_writeq_low_water()
{
    return gateway.writeq_low_water;
}
/**
 * Return the configured number of milliseconds for which we wait when we do
 * a blocking poll call.
 *
 * @return The number of milliseconds to sleep in a blocking poll call
 */
unsigned int
config_pollsleep()
{
    return gateway.pollsleep;
}

static struct
{
    const char* name;
    int         priority;
    const char* replacement;
} lognames[] =
{
    { "log_messages", LOG_NOTICE,  "log_notice" }, // Deprecated
    { "log_trace",    LOG_INFO,    "log_info" },   // Deprecated
    { "log_debug",    LOG_DEBUG,   NULL },
    { "log_warning",  LOG_WARNING, NULL },
    { "log_notice",   LOG_NOTICE,  NULL },
    { "log_info",     LOG_INFO,    NULL },
    { NULL, 0 }
};

/**
 * Configuration handler for items in the global [MaxScale] section
 *
 * @param name  The item name
 * @param value The item value
 * @return 0 on error
 */
static  int
handle_global_item(const char *name, const char *value)
{
    bool processed = true; // assume 'name' is valid

    int i;
    if (strcmp(name, CN_THREADS) == 0)
    {
        if (strcmp(value, CN_AUTO) == 0)
        {
            gateway.n_threads = get_processor_count();
        }
        else
        {
            int thrcount = atoi(value);
            if (thrcount > 0)
            {
                gateway.n_threads = thrcount;

                int processor_count = get_processor_count();
                if (thrcount > processor_count)
                {
                    MXS_WARNING("Number of threads set to %d, which is greater than "
                                "the number of processors available: %d",
                                thrcount, processor_count);
                }
            }
            else
            {
                MXS_WARNING("Invalid value for 'threads': %s.", value);
                return 0;
            }
        }

        if (gateway.n_threads > MXS_MAX_ROUTING_THREADS)
        {
            MXS_WARNING("Number of threads set to %d, which is greater than the "
                        "hard maximum of %d. Number of threads adjusted down "
                        "accordingly.", gateway.n_threads, MXS_MAX_ROUTING_THREADS);
            gateway.n_threads = MXS_MAX_ROUTING_THREADS;
        }
    }
    else if (strcmp(name, CN_THREAD_STACK_SIZE) == 0)
    {
        // DEPRECATED in 2.3, remove in 2.4
        MXS_WARNING("%s is ignored and has been deprecated. If you need to explicitly "
                    "set the stack size, do so with 'ulimit -s' before starting MaxScale.",
                    CN_THREAD_STACK_SIZE);
    }
    else if (strcmp(name, CN_NON_BLOCKING_POLLS) == 0)
    {
        // DEPRECATED in 2.3, remove in 2.4
        MXS_WARNING("The configuration option '%s' has no meaning and has been deprecated.",
                    CN_NON_BLOCKING_POLLS);
        gateway.n_nbpoll = atoi(value);
    }
    else if (strcmp(name, CN_POLL_SLEEP) == 0)
    {
        // DEPRECATED in 2.3, remove in 2.4
        MXS_WARNING("The configuration option '%s' has no meaning and has been deprecated.",
                    CN_POLL_SLEEP);
        gateway.pollsleep = atoi(value);
    }
    else if (strcmp(name, CN_MS_TIMESTAMP) == 0)
    {
        mxs_log_set_highprecision_enabled(config_truth_value((char*)value));
    }
    else if (strcmp(name, CN_SKIP_PERMISSION_CHECKS) == 0)
    {
        gateway.skip_permission_checks = config_truth_value((char*)value);
    }
    else if (strcmp(name, CN_AUTH_CONNECT_TIMEOUT) == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval > 0)
        {
            gateway.auth_conn_timeout = intval;
        }
        else
        {
            MXS_WARNING("Invalid timeout value for 'auth_connect_timeout': %s", value);
        }
    }
    else if (strcmp(name, CN_AUTH_READ_TIMEOUT) == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval > 0)
        {
            gateway.auth_read_timeout = intval;
        }
        else
        {
            MXS_ERROR("Invalid timeout value for 'auth_read_timeout': %s", value);
        }
    }
    else if (strcmp(name, CN_AUTH_WRITE_TIMEOUT) == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval > 0)
        {
            gateway.auth_write_timeout = intval;
        }
        else
        {
            MXS_ERROR("Invalid timeout value for 'auth_write_timeout': %s", value);
        }
    }
    else if (strcmp(name, CN_QUERY_CLASSIFIER) == 0)
    {
        int len = strlen(value);
        int max_len = sizeof(gateway.qc_name) - 1;

        if (len <= max_len)
        {
            strcpy(gateway.qc_name, value);
        }
        else
        {
            MXS_ERROR("The length of '%s' is %d, while the maximum length is %d.",
                      value, len, max_len);
            return 0;
        }
    }
    else if (strcmp(name, CN_QUERY_CLASSIFIER_ARGS) == 0)
    {
        gateway.qc_args = MXS_STRDUP_A(value);
    }
    else if (strcmp(name, CN_QUERY_CLASSIFIER_CACHE_SIZE) == 0)
    {
        uint64_t int_value;

        if (!get_suffixed_size(value, &int_value))
        {
            MXS_ERROR("Invalid value for %s: %s", CN_QUERY_CLASSIFIER_CACHE_SIZE, value);
            return 0;
        }

        decltype(gateway.qc_cache_properties.max_size) max_size = int_value;

        if (max_size >= 0)
        {
            gateway.qc_cache_properties.max_size = max_size;
        }
        else
        {
            MXS_ERROR("Value too large for %s: %s", CN_QUERY_CLASSIFIER_CACHE_SIZE, value);
            return 0;
        }
    }
    else if (strcmp(name, "sql_mode") == 0)
    {
        if (strcasecmp(value, "default") == 0)
        {
            gateway.qc_sql_mode = QC_SQL_MODE_DEFAULT;
        }
        else if (strcasecmp(value, "oracle") == 0)
        {
            gateway.qc_sql_mode = QC_SQL_MODE_ORACLE;
        }
        else
        {
            MXS_ERROR("'%s' is not a valid value for '%s'. Allowed values are 'DEFAULT' and "
                      "'ORACLE'. Using 'DEFAULT' as default.", value, name);
        }
    }
    else if (strcmp(name, CN_QUERY_RETRIES) == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval >= 0)
        {
            gateway.query_retries = intval;
        }
        else
        {
            MXS_ERROR("Invalid timeout value for '%s': %s", CN_QUERY_RETRIES, value);
            return 0;
        }
    }
    else if (strcmp(name, CN_QUERY_RETRY_TIMEOUT) == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval > 0)
        {
            gateway.query_retry_timeout = intval;
        }
        else
        {
            MXS_ERROR("Invalid timeout value for '%s': %s", CN_QUERY_RETRY_TIMEOUT, value);
            return 0;
        }
    }
    else if (strcmp(name, CN_LOG_THROTTLING) == 0)
    {
        if (*value == 0)
        {
            MXS_LOG_THROTTLING throttling = { 0, 0, 0 };

            mxs_log_set_throttling(&throttling);
        }
        else
        {
            char *v = MXS_STRDUP_A(value);

            char *count = v;
            char *window_ms = NULL;
            char *suppress_ms = NULL;

            window_ms = strchr(count, ',');
            if (window_ms)
            {
                *window_ms = 0;
                ++window_ms;

                suppress_ms = strchr(window_ms, ',');
                if (suppress_ms)
                {
                    *suppress_ms = 0;
                    ++suppress_ms;
                }
            }

            if (!count || !window_ms || !suppress_ms)
            {
                MXS_ERROR("Invalid value for the `log_throttling` configuration entry: \"%s\". "
                          "No throttling will now be performed.", value);
                MXS_NOTICE("The format of the value for 'log_throttling' is \"X, Y, Z\", where "
                           "X is the maximum number of times a particular error can be logged "
                           "in the time window of Y milliseconds, before the logging is suppressed "
                           "for Z milliseconds.");
            }
            else
            {
                int c = atoi(count);
                int w = atoi(window_ms);
                int s = atoi(suppress_ms);

                if ((c >= 0) && (w >= 0) && (s >= 0))
                {
                    MXS_LOG_THROTTLING throttling;
                    throttling.count = c;
                    throttling.window_ms = w;
                    throttling.suppress_ms = s;

                    mxs_log_set_throttling(&throttling);
                }
                else
                {
                    MXS_ERROR("Invalid value for the `log_throttling` configuration entry: \"%s\". "
                              "No throttling will now be performed.", value);
                    MXS_NOTICE("The configuration entry 'log_throttling' requires as value three positive "
                               "integers (or 0).");
                }
            }

            MXS_FREE(v);
        }
    }
    else if (strcmp(name, CN_ADMIN_PORT) == 0)
    {
        gateway.admin_port = atoi(value);
    }
    else if (strcmp(name, CN_ADMIN_HOST) == 0)
    {
        strcpy(gateway.admin_host, value);
    }
    else if (strcmp(name, CN_ADMIN_SSL_KEY) == 0)
    {
        strcpy(gateway.admin_ssl_key, value);
    }
    else if (strcmp(name, CN_ADMIN_SSL_CERT) == 0)
    {
        strcpy(gateway.admin_ssl_cert, value);
    }
    else if (strcmp(name, CN_ADMIN_SSL_CA_CERT) == 0)
    {
        strcpy(gateway.admin_ssl_ca_cert, value);
    }
    else if (strcmp(name, CN_ADMIN_AUTH) == 0)
    {
        gateway.admin_auth = config_truth_value(value);
    }
    else if (strcmp(name, CN_ADMIN_ENABLED) == 0)
    {
        gateway.admin_enabled = config_truth_value(value);
    }
    else if (strcmp(name, CN_ADMIN_LOG_AUTH_FAILURES) == 0)
    {
        gateway.admin_log_auth_failures = config_truth_value(value);
    }
    else if (strcmp(name, CN_PASSIVE) == 0)
    {
        gateway.passive = config_truth_value((char*)value);
    }
    else if (strcmp(name, CN_LOCAL_ADDRESS) == 0)
    {
        gateway.local_address = MXS_STRDUP_A(value);
    }
    else if (strcmp(name, CN_USERS_REFRESH_TIME) == 0)
    {
        char* endptr;
        long users_refresh_time = strtol(value, &endptr, 0);
        if (*endptr == '\0')
        {
            if (users_refresh_time < 0)
            {
                MXS_NOTICE("Value of '%s' is less than 0, users will "
                           "not be automatically refreshed.", CN_USERS_REFRESH_TIME);
                // Strictly speaking they will be refreshed once every 68 years,
                // but I just don't beleave the uptime will be that long.
                users_refresh_time = INT32_MAX;
            }
            else if (users_refresh_time < USERS_REFRESH_TIME_MIN)
            {
                MXS_WARNING("%s is less than the allowed minimum value of %d for the "
                            "configuration option '%s', using the minimum value.",
                            value, USERS_REFRESH_TIME_MIN, CN_USERS_REFRESH_TIME);
                users_refresh_time = USERS_REFRESH_TIME_MIN;
            }

            if (users_refresh_time > INT32_MAX)
            {
                // To ensure that there will be no overflows when
                // we later do arithmetic.
                users_refresh_time = INT32_MAX;
            }

            gateway.users_refresh_time = users_refresh_time;
        }
        else if (strcmp(name, CN_WRITEQ_HIGH_WATER) == 0)
        {
            if (!get_suffixed_size(value, &gateway.writeq_high_water))
            {
                MXS_ERROR("Invalid value for %s: %s", CN_WRITEQ_HIGH_WATER, value);
                return 0;
            }

            if (gateway.writeq_high_water < MIN_WRITEQ_HIGH_WATER)
            {
                MXS_WARNING("The specified writeq high water mark %lu, is smaller "
                            "than the minimum allowed size %lu. Changing to minimum.",
                            gateway.writeq_high_water, MIN_WRITEQ_HIGH_WATER);
                gateway.writeq_high_water = MIN_WRITEQ_HIGH_WATER;
            }
            MXS_NOTICE("Writeq high water mark set to: %lu", gateway.writeq_high_water);
        }
        else if (strcmp(name, CN_WRITEQ_LOW_WATER) == 0)
        {
            if (!get_suffixed_size(value, &gateway.writeq_high_water))
            {
                MXS_ERROR("Invalid value for %s: %s", CN_WRITEQ_HIGH_WATER, value);
                return 0;
            }

            if (gateway.writeq_low_water < MIN_WRITEQ_LOW_WATER)
            {
                MXS_WARNING("The specified writeq low water mark %lu, is smaller "
                            "than the minimum allowed size %lu. Changing to minimum.",
                            gateway.writeq_low_water, MIN_WRITEQ_LOW_WATER);
                gateway.writeq_low_water = MIN_WRITEQ_LOW_WATER;
            }
            MXS_NOTICE("Writeq low water mark set to: %lu", gateway.writeq_low_water);
        }
        else
        {
            MXS_ERROR("%s is an invalid value for '%s', using default %d instead.",
                      value, CN_USERS_REFRESH_TIME, USERS_REFRESH_TIME_DEFAULT);
            gateway.users_refresh_time = USERS_REFRESH_TIME_DEFAULT;
        }
    }
    else if (strcmp(name, CN_RETAIN_LAST_STATEMENTS) == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval >= 0)
        {
            session_set_retain_last_statements(intval);
        }
        else
        {
            MXS_ERROR("Invalid value for '%s': %s", CN_RETAIN_LAST_STATEMENTS, value);
        }
    }
    else if (strcmp(name, CN_DUMP_LAST_STATEMENTS) == 0)
    {
        if (strcmp(value, "on_close") == 0)
        {
            session_set_dump_statements(SESSION_DUMP_STATEMENTS_ON_CLOSE);
        }
        else if (strcmp(value, "on_error") == 0)
        {
            session_set_dump_statements(SESSION_DUMP_STATEMENTS_ON_ERROR);
        }
        else if (strcmp(value, "never") == 0)
        {
            session_set_dump_statements(SESSION_DUMP_STATEMENTS_NEVER);
        }
        else
        {
            MXS_ERROR("%s can have the values 'never', 'on_close' or 'on_error'.",
                      CN_DUMP_LAST_STATEMENTS);
        }
    }
    else if (strcmp(name, CN_PEER_HOSTS) == 0)
    {
        if (strchr(value, ','))
        {
            MXS_ERROR("Only a single host in '%s' is currently supported", CN_PEER_HOSTS);
            return 0;
        }
        else
        {
            strcpy(gateway.peer_hosts, value);
        }
    }
    else if (strcmp(name, CN_PEER_USER) == 0)
    {
        strcpy(gateway.peer_user, value);
    }
    else if (strcmp(name, CN_PEER_PASSWORD) == 0)
    {
        char* pw = decrypt_password(value);
        strcpy(gateway.peer_password, pw);
        MXS_FREE(pw);
    }
    else
    {
        bool found = false;
#ifndef SS_DEBUG
        if (strcmp(name, "log_debug") == 0)
        {
            MXS_WARNING("The 'log_debug' option has no effect in release mode.");
            found = true;
        }
        else
#endif
        {
            maxscale::event::result_t result = maxscale::event::configure(name, value);

            switch (result)
            {
            case maxscale::event::ACCEPTED:
                found = true;
                break;

            case maxscale::event::IGNORED:
                for (i = 0; lognames[i].name; i++)
                {
                    if (strcasecmp(name, lognames[i].name) == 0)
                    {
                        found = true;
                        if (lognames[i].replacement)
                        {
                            MXS_WARNING("In the configuration file the use of '%s' is deprecated, "
                                        "use '%s' instead.",
                                        lognames[i].name, lognames[i].replacement);
                        }

                        mxs_log_set_priority_enabled(lognames[i].priority, config_truth_value(value));
                    }
                }
                break;

            case maxscale::event::INVALID:
                // TODO: Should we bail out?
                break;
            }
        }


        if (!found)
        {
            for (int i = 0; !found && config_pre_parse_global_params[i]; ++i)
            {
                found = strcmp(name, config_pre_parse_global_params[i]) == 0;
            }
        }
        processed = found;
    }

    if (!processed)
    {
        MXS_WARNING("Config entry '%s' is unknown."
                    " Please remove it from the configuration file.", name);
    }

    return 1;
}

/**
 * Free an SSL structure
 *
 * @param ssl SSL structure to free
 */
static void
free_ssl_structure(SSL_LISTENER *ssl)
{
    if (ssl)
    {
        SSL_CTX_free(ssl->ctx);
        MXS_FREE(ssl->ssl_key);
        MXS_FREE(ssl->ssl_cert);
        MXS_FREE(ssl->ssl_ca_cert);
        MXS_FREE(ssl);
    }
}

bool config_create_ssl(const char* name, MXS_CONFIG_PARAMETER* params,
                       bool require_cert, SSL_LISTENER** dest)
{
    SSL_LISTENER* ssl = NULL;

    // The enum values convert to bool
    int value = config_get_enum(params, CN_SSL, ssl_values);
    mxb_assert(value != -1);

    if (value)
    {
        bool error = false;
        char* ssl_cert = config_get_value(params, CN_SSL_CERT);
        char* ssl_key = config_get_value(params, CN_SSL_KEY);
        char* ssl_ca_cert = config_get_value(params, CN_SSL_CA_CERT);

        if (ssl_ca_cert == NULL)
        {
            MXS_ERROR("CA Certificate missing for '%s'."
                      "Please provide the path to the certificate authority "
                      "certificate by adding the ssl_ca_cert=<path> parameter",
                      name);
            error = true;
        }

        if (require_cert)
        {
            if (ssl_cert == NULL)
            {
                MXS_ERROR("Server certificate missing for listener '%s'."
                          "Please provide the path to the server certificate by adding "
                          "the ssl_cert=<path> parameter", name);
                error = true;
            }

            if (ssl_key == NULL)
            {
                MXS_ERROR("Server private key missing for listener '%s'. "
                          "Please provide the path to the server certificate key by "
                          "adding the ssl_key=<path> parameter", name);
                error = true;
            }
        }

        if (error)
        {
            return false;
        }

        ssl = (SSL_LISTENER*)MXS_CALLOC(1, sizeof(SSL_LISTENER));
        MXS_ABORT_IF_NULL(ssl);

        int ssl_version = config_get_enum(params, CN_SSL_VERSION, ssl_version_values);

        ssl->ssl_method_type = (ssl_method_type_t)ssl_version;
        ssl->ssl_init_done = false;
        ssl->ssl_cert_verify_depth = config_get_integer(params, CN_SSL_CERT_VERIFY_DEPTH);
        ssl->ssl_verify_peer_certificate = config_get_bool(params, CN_SSL_VERIFY_PEER_CERTIFICATE);

        listener_set_certificates(ssl, ssl_cert, ssl_key, ssl_ca_cert);

        mxb_assert(access(ssl_ca_cert, F_OK) == 0);
        mxb_assert(!ssl_cert || access(ssl_cert, F_OK) == 0);
        mxb_assert(!ssl_key || access(ssl_key, F_OK) == 0);

        if (!SSL_LISTENER_init(ssl))
        {
            SSL_LISTENER_free(ssl);
            return false;
        }
    }

    *dest = ssl;
    return true;
}

void config_set_global_defaults()
{
    uint8_t mac_addr[6] = "";
    struct utsname uname_data;
    gateway.config_check = false;
    gateway.n_threads = DEFAULT_NTHREADS;
    gateway.n_nbpoll = DEFAULT_NBPOLLS;
    gateway.pollsleep = DEFAULT_POLLSLEEP;
    gateway.auth_conn_timeout = DEFAULT_AUTH_CONNECT_TIMEOUT;
    gateway.auth_read_timeout = DEFAULT_AUTH_READ_TIMEOUT;
    gateway.auth_write_timeout = DEFAULT_AUTH_WRITE_TIMEOUT;
    gateway.skip_permission_checks = false;
    gateway.syslog = 1;
    gateway.maxlog = 1;
    gateway.admin_port = DEFAULT_ADMIN_HTTP_PORT;
    gateway.admin_auth = true;
    gateway.admin_log_auth_failures = true;
    gateway.admin_enabled = true;
    strcpy(gateway.admin_host, DEFAULT_ADMIN_HOST);
    gateway.admin_ssl_key[0] = '\0';
    gateway.admin_ssl_cert[0] = '\0';
    gateway.admin_ssl_ca_cert[0] = '\0';
    gateway.query_retries = DEFAULT_QUERY_RETRIES;
    gateway.query_retry_timeout = DEFAULT_QUERY_RETRY_TIMEOUT;
    gateway.passive = false;
    gateway.promoted_at = 0;

    gateway.peer_hosts[0] = '\0';
    gateway.peer_user[0] = '\0';
    gateway.peer_password[0] = '\0';
    gateway.log_target = MXB_LOG_TARGET_DEFAULT;

    // Note: This is not a valid cache value: it is used to detect that the default value is used
    gateway.qc_cache_properties.max_size = -1;

    gateway.thread_stack_size = 0;
    gateway.writeq_high_water = 0;
    gateway.writeq_low_water = 0;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) == 0)
    {
        size_t thread_stack_size;
        if (pthread_attr_getstacksize(&attr, &thread_stack_size) == 0)
        {
            gateway.thread_stack_size = thread_stack_size;
        }
    }

    if (version_string != NULL)
    {
        gateway.version_string = MXS_STRDUP_A(version_string);
    }
    else
    {
        gateway.version_string = NULL;
    }

    /* get release string */
    if (!config_get_release_string(gateway.release_string))
    {
        sprintf(gateway.release_string, "undefined");
    }

    /* get first mac_address in SHA1 */
    if (config_get_ifaddr(mac_addr))
    {
        gw_sha1_str(mac_addr, 6, gateway.mac_sha1);
    }
    else
    {
        memset(gateway.mac_sha1, '\0', sizeof(gateway.mac_sha1));
        memcpy(gateway.mac_sha1, "MAC-undef", 9);
    }

    /* get uname info */
    if (uname(&uname_data))
    {
        strcpy(gateway.sysname, "undefined");
    }
    else
    {
        strcpy(gateway.sysname, uname_data.sysname);
    }

    /* query_classifier */
    memset(gateway.qc_name, 0, sizeof(gateway.qc_name));
    gateway.qc_args = NULL;
    gateway.qc_sql_mode = QC_SQL_MODE_DEFAULT;
}

/**
 * @brief Check if required parameters are missing
 *
 * @param name Module name
 * @param type Module type
 * @param params List of parameters for the object
 * @return True if at least one of the required parameters is missing
 */
static bool missing_required_parameters(const MXS_MODULE_PARAM *mod_params,
                                        MXS_CONFIG_PARAMETER *params,
                                        const char* name)
{
    bool rval = false;

    if (mod_params)
    {
        for (int i = 0; mod_params[i].name; i++)
        {
            if ((mod_params[i].options & MXS_MODULE_OPT_REQUIRED) &&
                config_get_param(params, mod_params[i].name) == NULL)
            {
                MXS_ERROR("Mandatory parameter '%s' is not defined for '%s'.",
                          mod_params[i].name, name);
                rval = true;
            }
        }
    }

    return rval;
}

static bool is_path_parameter(const MXS_MODULE_PARAM *params, const char *name)
{
    bool rval = false;

    if (params)
    {
        for (int i = 0; params[i].name; i++)
        {
            if (strcmp(params[i].name, name) == 0 && params[i].type == MXS_MODULE_PARAM_PATH)
            {
                rval = true;
                break;
            }
        }
    }

    return rval;
}

static void process_path_parameter(MXS_CONFIG_PARAMETER *param)
{
    if (*param->value != '/')
    {
        const char *mod_dir = get_module_configdir();
        size_t size = strlen(param->value) + strlen(mod_dir) + 3;
        char *value = (char*)MXS_MALLOC(size);
        MXS_ABORT_IF_NULL(value);

        sprintf(value, "/%s/%s", mod_dir, param->value);
        clean_up_pathname(value);
        MXS_FREE(param->value);
        param->value = value;
    }
}

static bool param_is_deprecated(const MXS_MODULE_PARAM* params, const char* name, const char* modname)
{
    bool rval = false;

    for (int i = 0; params[i].name; i++)
    {
        if (strcmp(params[i].name, name) == 0)
        {
            if (params[i].options & MXS_MODULE_OPT_DEPRECATED)
            {
                MXS_WARNING("Parameter '%s' for module '%s' is deprecated and "
                            "will be ignored.", name, modname);
                rval = true;
            }
            break;
        }
    }

    return rval;
}

static bool param_in_set(const MXS_MODULE_PARAM* params, const char* name)
{
    bool found = false;

    for (int i = 0; params[i].name; i++)
    {
        if (strcmp(params[i].name, name) == 0)
        {
            found = true;
            break;
        }
    }

    return found;
}

const char* param_type_to_str(const MXS_MODULE_PARAM* params, const char* name)
{

    for (int i = 0; params[i].name; i++)
    {
        if (strcmp(params[i].name, name) == 0)
        {
            switch (params[i].type)
            {
            case MXS_MODULE_PARAM_COUNT:
                return "a non-negative integer";

            case MXS_MODULE_PARAM_INT:
                return "an integer";

            case MXS_MODULE_PARAM_SIZE:
                return "a size in bytes (e.g. 1M)";

            case MXS_MODULE_PARAM_BOOL:
                return "a boolean value";

            case MXS_MODULE_PARAM_STRING:
                return "a string";

            case MXS_MODULE_PARAM_QUOTEDSTRING:
                return "a quoted string";

            case MXS_MODULE_PARAM_REGEX:
                return "a regular expression";

            case MXS_MODULE_PARAM_ENUM:
                return "an enumeration value";

            case MXS_MODULE_PARAM_SERVICE:
                return "a service name";

            case MXS_MODULE_PARAM_SERVER:
                return "a server name";

            case MXS_MODULE_PARAM_SERVERLIST:
                return "a comma-separated list of server names";

            case MXS_MODULE_PARAM_PATH:
                return "a path to a file";

            default:
                mxb_assert_message(!true, "Unknown parameter type");
                return "<unknown parameter type>";
            }
        }
    }

    mxb_assert_message(!true, "Unknown parameter name");
    return "<unknown parameter name>";
}

/**
 * @brief Check that the configuration objects have valid parameters
 *
 * @param context Configuration context
 * @return True if the configuration is OK, false if errors were detected
 */
static bool
check_config_objects(CONFIG_CONTEXT *context)
{
    bool rval = true;

    for (CONFIG_CONTEXT* obj = context; obj; obj = obj->next)
    {
        if (is_maxscale_section(obj->object))
        {
            continue;
        }

        std::string type = config_get_string(obj->parameters, CN_TYPE);

        if (!valid_object_type(type))
        {
            MXS_ERROR("Unknown module type for object '%s': %s", obj->object, type.c_str());
            rval = false;
            continue;
        }

        const MXS_MODULE_PARAM* param_set = nullptr;
        const MXS_MODULE *mod = nullptr;
        std::tie(param_set, mod) = get_module_details(obj);

        if (!mod) // Error is logged in load_module
        {
            rval = false;
            continue;
        }

        mxb_assert(param_set);
        std::vector<std::string> to_be_removed;

        for (MXS_CONFIG_PARAMETER *params = obj->parameters; params; params = params->next)
        {
            const MXS_MODULE_PARAM* fix_params;

            if (param_in_set(param_set, params->name))
            {
                fix_params = param_set;
            }
            else if (param_in_set(mod->parameters, params->name))
            {
                fix_params = mod->parameters;
            }
            else
            {
                // Server's "need" to ignore any unknown parameters as they could
                // be used as weighting parameters
                if (type != CN_SERVER)
                {
                    MXS_ERROR("Unknown parameter '%s' for object '%s' of type '%s'",
                              params->name, obj->object, type.c_str());
                    rval = false;
                }
                continue;
            }

            if (config_param_is_valid(fix_params, params->name, params->value, context))
            {
                if (is_path_parameter(fix_params, params->name))
                {
                    process_path_parameter(params);
                }
                else // Fix old-style object names
                {
                    config_fix_param(fix_params, params);
                }

                if (param_is_deprecated(fix_params, params->name, obj->object))
                {
                    to_be_removed.push_back(params->name);
                }
            }
            else
            {
                MXS_ERROR("Invalid value for parameter '%s' for object '%s' "
                          "of type '%s': %s (was expecting %s)",
                          params->name, params->value, obj->object, type.c_str(),
                          param_type_to_str(fix_params, params->name));
                rval = false;
            }
        }

        for (auto it = to_be_removed.begin(); it != to_be_removed.end(); it++)
        {
            config_remove_param(obj, it->c_str());
        }

        if (missing_required_parameters(param_set, obj->parameters, obj->object) ||
            missing_required_parameters(mod->parameters, obj->parameters, obj->object))
        {
            rval = false;
        }
    }

    return rval;
}

int
config_truth_value(const char *str)
{
    if (strcasecmp(str, "true") == 0 || strcasecmp(str, "on") == 0 ||
        strcasecmp(str, "yes") == 0 || strcasecmp(str, "1") == 0)
    {
        return 1;
    }
    if (strcasecmp(str, "false") == 0 || strcasecmp(str, "off") == 0 ||
        strcasecmp(str, "no") == 0 || strcasecmp(str, "0") == 0)
    {
        return 0;
    }

    return -1;
}

/**
 * Get the MAC address of first network interface
 *
 * and fill the provided allocated buffer with SHA1 encoding
 * @param output        Allocated 6 bytes buffer
 * @return 1 on success, 0 on failure
 *
 */
int
config_get_ifaddr(unsigned char *output)
{
    struct ifreq ifr;
    struct ifconf ifc;
    char buf[1024];
    struct ifreq* it;
    struct ifreq* end;
    int success = 0;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock == -1)
    {
        return 0;
    }

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1)
    {
        close(sock);
        return 0;
    }

    it = ifc.ifc_req;
    end = it + (ifc.ifc_len / sizeof(struct ifreq));

    for (; it != end; ++it)
    {
        strcpy(ifr.ifr_name, it->ifr_name);

        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
        {
            if (!(ifr.ifr_flags & IFF_LOOPBACK))
            {
                /* don't count loopback */
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0)
                {
                    success = 1;
                    break;
                }
            }
        }
        else
        {
            close(sock);
            return 0;
        }
    }

    if (success)
    {
        memcpy(output, ifr.ifr_hwaddr.sa_data, 6);
    }
    close(sock);

    return success;
}

/**
 * Get the linux distribution info
 *
 * @param release The buffer where the found distribution is copied.
 *                Assumed to be at least _RELEASE_STR_LENGTH bytes.
 *
 * @return 1 on success, 0 on failure
 */
static int
config_get_release_string(char* release)
{
    const char *masks[] =
    {
        "/etc/*-version", "/etc/*-release",
        "/etc/*_version", "/etc/*_release"
    };

    bool have_distribution;
    char distribution[RELEASE_STR_LENGTH] = "";
    int fd;

    have_distribution = false;

    /* get data from lsb-release first */
    if ((fd = open("/etc/lsb-release", O_RDONLY)) != -1)
    {
        /* LSB-compliant distribution! */
        size_t len = read(fd, (char*)distribution, sizeof(distribution) - 1);
        close(fd);

        if (len != (size_t) - 1)
        {
            distribution[len] = 0;

            char *found = strstr(distribution, "DISTRIB_DESCRIPTION=");

            if (found)
            {
                have_distribution = true;
                char *end = strstr(found, "\n");
                if (end == NULL)
                {
                    end = distribution + len;
                }
                found += 20; // strlen("DISTRIB_DESCRIPTION=")

                if (*found == '"' && end[-1] == '"')
                {
                    found++;
                    end--;
                }
                *end = 0;

                char *to = strcpy(distribution, "lsb: ");
                memmove(to, found, end - found + 1 < INT_MAX ? end - found + 1 : INT_MAX);

                strcpy(release, to);

                return 1;
            }
        }
    }

    /* if not an LSB-compliant distribution */
    for (int i = 0; !have_distribution && i < 4; i++)
    {
        glob_t found;
        char *new_to;

        if (glob(masks[i], GLOB_NOSORT, NULL, &found) == 0)
        {
            int fd;
            size_t k = 0;
            int skipindex = 0;
            int startindex = 0;

            for (k = 0; k < found.gl_pathc; k++)
            {
                if (strcmp(found.gl_pathv[k], "/etc/lsb-release") == 0)
                {
                    skipindex = k;
                }
            }

            if (skipindex == 0)
            {
                startindex++;
            }

            if ((fd = open(found.gl_pathv[startindex], O_RDONLY)) != -1)
            {
                /*
                  +5 and -8 below cut the file name part out of the
                  full pathname that corresponds to the mask as above.
                */
                new_to = strncpy(distribution, found.gl_pathv[0] + 5, RELEASE_STR_LENGTH - 1);
                new_to += 8;
                *new_to++ = ':';
                *new_to++ = ' ';

                size_t to_len = distribution + sizeof(distribution) - 1 - new_to;
                size_t len = read(fd, (char*)new_to, to_len);

                close(fd);

                if (len != (size_t) - 1)
                {
                    new_to[len] = 0;
                    char *end = strstr(new_to, "\n");
                    if (end)
                    {
                        *end = 0;
                    }

                    have_distribution = true;
                    strncpy(release, new_to, RELEASE_STR_LENGTH - 1);
                    release[RELEASE_STR_LENGTH - 1] = '\0';
                }
            }
        }
        globfree(&found);
    }

    if (have_distribution)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

MXS_CONFIG* config_get_global_options()
{
    return &gateway;
}

/**
 * Check if sections are defined multiple times in the configuration file.
 *
 * @param filename Path to the configuration file
 * @param context  The context object used for tracking the duplication
 *                 section information.
 *
 * @return True if duplicate sections were found or an error occurred
 */
bool config_has_duplicate_sections(const char* filename, DUPLICATE_CONTEXT* context)
{
    bool rval = false;

    int size = 1024;
    char *buffer = (char*)MXS_MALLOC(size * sizeof(char));

    if (buffer)
    {
        FILE* file = fopen(filename, "r");

        if (file)
        {
            while (maxscale_getline(&buffer, &size, file) > 0)
            {
                if (pcre2_match(context->re, (PCRE2_SPTR) buffer,
                                PCRE2_ZERO_TERMINATED, 0, 0,
                                context->mdata, NULL) > 0)
                {
                    /**
                     * Neither of the PCRE2 calls will fail since we know the pattern
                     * beforehand and we allocate enough memory from the stack
                     */
                    PCRE2_SIZE len;
                    pcre2_substring_length_bynumber(context->mdata, 1, &len);
                    len += 1; /** one for the null terminator */
                    PCRE2_UCHAR section[len];
                    pcre2_substring_copy_bynumber(context->mdata, 1, section, &len);

                    string key(reinterpret_cast<char*>(section), len);
                    if (context->sections->insert(key).second == false)
                    {
                        MXS_ERROR("Duplicate section found: %s", section);
                        rval = true;
                    }
                }
            }
            fclose(file);
        }
        else
        {
            MXS_ERROR("Failed to open file '%s': %s", filename, mxs_strerror(errno));
            rval = true;
        }
    }
    else
    {
        MXS_OOM_MESSAGE("Failed to allocate enough memory when checking"
                        " for duplicate sections in configuration file.");
        rval = true;
    }

    MXS_FREE(buffer);
    return rval;
}


/**
 * Read from a FILE pointer until a newline character or the end of the file is found.
 * The provided buffer will be reallocated if it is too small to store the whole
 * line. The size after the reallocation will be stored in @c size. The read line
 * will be stored in @c dest and it will always be null terminated. The newline
 * character will not be copied into the buffer.
 * @param dest Pointer to a buffer of at least @c size bytes
 * @param size Size of the buffer
 * @param file A valid file stream
 * @return When a complete line was successfully read the function returns 1. If
 * the end of the file was reached before any characters were read the return value
 * will be 0. If the provided buffer could not be reallocated to store the complete
 * line the original size will be retained, everything read up to this point
 * will be stored in it as a null terminated string and -1 will be returned.
 */
int maxscale_getline(char** dest, int* size, FILE* file)
{
    char* destptr = *dest;
    int offset = 0;

    if (feof(file) || ferror(file))
    {
        return 0;
    }

    while (true)
    {
        if (*size <= offset)
        {
            char* tmp = (char*)MXS_REALLOC(destptr, *size * 2);
            if (tmp)
            {
                destptr = tmp;
                *size *= 2;
            }
            else
            {
                destptr[offset - 1] = '\0';
                *dest = destptr;
                return -1;
            }
        }

        int c = fgetc(file);

        if ((c == '\n') || (c == EOF))
        {
            destptr[offset] = '\0';
            break;
        }
        else
        {
            destptr[offset] = c;
        }

        offset++;
    }

    *dest = destptr;
    return 1;
}

/**
 * Validate the SSL parameters for a service
 * @param ssl_cert SSL certificate (private key)
 * @param ssl_ca_cert SSL CA certificate
 * @param ssl_key SSL key (public key)
 * @return 0 if parameters are valid otherwise the number of errors if errors
 * were detected
 */
static int validate_ssl_parameters(CONFIG_CONTEXT* obj, char *ssl_cert, char *ssl_ca_cert, char *ssl_key)
{
    int error_count = 0;
    if (ssl_cert == NULL)
    {
        error_count++;
        MXS_ERROR("Server certificate missing for listener '%s'."
                  "Please provide the path to the server certificate by adding "
                  "the ssl_cert=<path> parameter", obj->object);
    }
    else if (access(ssl_cert, F_OK) != 0)
    {
        error_count++;
        MXS_ERROR("Server certificate file for listener '%s' not found: %s",
                  obj->object, ssl_cert);
    }

    if (ssl_ca_cert == NULL)
    {
        error_count++;
        MXS_ERROR("CA Certificate missing for listener '%s'."
                  "Please provide the path to the certificate authority "
                  "certificate by adding the ssl_ca_cert=<path> parameter",
                  obj->object);
    }
    else if (access(ssl_ca_cert, F_OK) != 0)
    {
        error_count++;
        MXS_ERROR("Certificate authority file for listener '%s' "
                  "not found: %s", obj->object, ssl_ca_cert);
    }

    if (ssl_key == NULL)
    {
        error_count++;
        MXS_ERROR("Server private key missing for listener '%s'. "
                  "Please provide the path to the server certificate key by "
                  "adding the ssl_key=<path> parameter", obj->object);
    }
    else if (access(ssl_key, F_OK) != 0)
    {
        error_count++;
        MXS_ERROR("Server private key file for listener '%s' not found: %s",
                  obj->object, ssl_key);
    }
    return error_count;
}

/**
 * @brief Add default parameters for a module to the configuration context
 *
 * Only parameters that aren't defined are added to the configuration context.
 * This allows users to override the default values.
 *
 * @param ctx Configuration context where the default parameters are added
 * @param module Name of the module
 */
void config_add_defaults(CONFIG_CONTEXT *ctx, const MXS_MODULE_PARAM *params)
{
    if (params)
    {
        for (int i = 0; params[i].name; i++)
        {
            if (params[i].default_value &&
                config_get_param(ctx->parameters, params[i].name) == NULL)
            {
                bool rv = config_add_param(ctx, params[i].name, params[i].default_value);
                MXS_ABORT_IF_FALSE(rv);
            }
        }
    }
}

/**
 * Convert a config value to a json object.
 *
 * @param param The parameter value to convert
 * @param param_info Type information for the parameter
 * @return Json integer, boolean or string
 */
static json_t* param_value_to_json(const MXS_CONFIG_PARAMETER* param, const MXS_MODULE_PARAM* param_info)
{
    mxb_assert(strcmp(param->name, param_info->name) == 0);
    json_t* rval = NULL;

    switch (param_info->type)
    {
    case MXS_MODULE_PARAM_COUNT:
    case MXS_MODULE_PARAM_INT:
        rval = json_integer(strtol(param->value, NULL, 10));
        break;

    case MXS_MODULE_PARAM_BOOL:
        rval = json_boolean(config_truth_value(param->value));
        break;

    default:
        rval = json_string(param->value);
        break;
    }

    return rval;
}

void config_add_module_params_json(const MXS_CONFIG_PARAMETER* parameters,
                                   const std::unordered_set<std::string>& ignored_params,
                                   const MXS_MODULE_PARAM* basic_params,
                                   const MXS_MODULE_PARAM* module_params,
                                   json_t* output)
{
    // Create a map of the config values to ease their extraction
    std::unordered_map<std::string, const MXS_CONFIG_PARAMETER*> params;

    for (const MXS_CONFIG_PARAMETER* p = parameters; p; p = p->next)
    {
        params[p->name] = p;
    }

    for (auto param_info : {basic_params, module_params})
    {
        for (int i = 0; param_info[i].name; i++)
        {
            if (ignored_params.count(param_info[i].name) == 0 &&
                !json_object_get(output, param_info[i].name))
            {
                if (auto item = params[param_info[i].name])
                {
                    json_object_set_new(output, param_info[i].name,
                                        param_value_to_json(item, &param_info[i]));
                }
                else
                {
                    // The parameter was not set in config and does not have a default value.
                    // Print a null value.
                    json_object_set_new(output, param_info[i].name, json_null());
                }
            }
        }
    }
}

/**
 * Create a new router for a service
 * @param obj Service configuration context
 * @return True if configuration was successful, false if an error occurred.
 */
int create_new_service(CONFIG_CONTEXT *obj)
{
    const char *router = config_get_string(obj->parameters, CN_ROUTER);
    mxb_assert(*router);

    char *user = config_get_value(obj->parameters, CN_USER);
    char *auth = config_get_value(obj->parameters, CN_PASSWORD);
    const MXS_MODULE* module = get_module(router, MODULE_ROUTER);
    mxb_assert(module);

    if ((!user || !auth) &&
        !rcap_type_required(module->module_capabilities, RCAP_TYPE_NO_AUTH))
    {
        MXS_ERROR("Service '%s' is missing %s%s%s.", obj->object,
                  user ? "" : "the 'user' parameter",
                  !user && !auth ? " and " : "",
                  auth ? "" : "the 'password' parameter");
        return 1;
    }

    config_add_defaults(obj, config_service_params);
    config_add_defaults(obj, module->parameters);

    Service* service = service_alloc(obj->object, router, obj->parameters);

    if (service)
    {
        int error_count = 0;

        for (auto& a : mxs::strtok(config_get_string(obj->parameters, CN_SERVERS), ","))
        {
            fix_object_name(a);

            if (SERVER * s = server_find_by_unique_name(a.c_str()))
            {
                serviceAddBackend(service, s);
            }
            else
            {
                MXS_ERROR("Unable to find server '%s' that is configured as part "
                          "of service '%s'.", a.c_str(), obj->object);
                error_count++;
            }
        }

        if (char* filters = config_get_value(obj->parameters, CN_FILTERS))
        {
            auto flist = mxs::strtok(filters, "|");

            if (!service->set_filters(flist))
            {
                error_count++;
            }
        }
    }
    else
    {
        MXS_ERROR("Service '%s' creation failed.", obj->object);
    }

    return service ? 0 : 1;
}

/**
 * Check if a parameter is a default server parameter.
 * @param param Parameter name
 * @return True if it is one of the standard server parameters
 */
bool is_normal_server_parameter(const char *param)
{
    for (int i = 0; config_server_params[i].name; i++)
    {
        if (strcmp(param, config_server_params[i].name) == 0)
        {
            return true;
        }
    }
    // Check if parameter is deprecated
    for (int i = 0; deprecated_server_params[i]; i++)
    {
        if (strcmp(param, deprecated_server_params[i]) == 0)
        {
            MXS_WARNING("Server parameter '%s' is deprecated and will be ignored.", param);
            return true;
        }
    }
    return false;
}

/**
 * Create a new server
 * @param obj Server configuration context
 * @return Number of errors
 */
int create_new_server(CONFIG_CONTEXT *obj)
{
    bool error = false;

    config_add_defaults(obj, config_server_params);

    const char* module = config_get_string(obj->parameters, CN_PROTOCOL);
    mxb_assert(module);

    if (const MXS_MODULE* mod = get_module(module, MODULE_PROTOCOL))
    {
        config_add_defaults(obj, mod->parameters);
    }
    else
    {
        MXS_ERROR("Unable to load protocol module '%s'.", module);
        return 1;
    }

    if (SERVER* server = server_alloc(obj->object, obj->parameters))
    {
        const char* disk_space_threshold = config_get_value(obj->parameters, CN_DISK_SPACE_THRESHOLD);
        if (disk_space_threshold)
        {
            if (!server_set_disk_space_threshold(server, disk_space_threshold))
            {
                MXS_ERROR("Invalid value for '%s' for server %s: %s",
                          CN_DISK_SPACE_THRESHOLD, server->name, disk_space_threshold);
                error = true;
            }
        }

    }
    else
    {
        MXS_ERROR("Failed to create a new server, memory allocation failed.");
        error = true;
    }

    return error;
}

/**
 * Create a new monitor
 *
 * @param obj               Monitor configuration context
 * @param monitored_servers Set containing the servers that are already monitored
 *
 * @return Number of errors
 */
int create_new_monitor(CONFIG_CONTEXT *obj, std::set<std::string>& monitored_servers)
{
    bool err = false;

    // TODO: Use server list parameter type for this
    for (auto& s : mxs::strtok(config_get_string(obj->parameters, CN_SERVERS), ","))
    {
        fix_object_name(s);
        SERVER* server = server_find_by_unique_name(s.c_str());

        if (!server)
        {
            MXS_ERROR("Unable to find server '%s' that is configured in "
                      "the monitor '%s'.", s.c_str(), obj->object);
            err = true;
        }
        else if (monitored_servers.insert(s.c_str()).second == false)
        {
            MXS_WARNING("Multiple monitors are monitoring server [%s]. "
                        "This will cause undefined behavior.", s.c_str());
        }
    }

    if (err)
    {
        return 1;
    }

    const char* module = config_get_string(obj->parameters, CN_MODULE);
    mxb_assert(module);

    if (const MXS_MODULE* mod = get_module(module, MODULE_MONITOR))
    {
        config_add_defaults(obj, config_monitor_params);
        config_add_defaults(obj, mod->parameters);
    }
    else
    {
        MXS_ERROR("Unable to load monitor module '%s'.", module);
        return 1;
    }

    MXS_MONITOR* monitor = monitor_create(obj->object, module, obj->parameters);

    if (monitor == NULL)
    {
        MXS_ERROR("Failed to create monitor '%s'.", obj->object);
        return 1;
    }

    int error_count = 0;

    // TODO: Parse this in the configuration
    const char* dst = config_get_value(obj->parameters, CN_DISK_SPACE_THRESHOLD);

    if (dst)
    {
        if (!monitor_set_disk_space_threshold(monitor, dst))
        {
            MXS_ERROR("Invalid value for '%s' for monitor %s: %s",
                      CN_DISK_SPACE_THRESHOLD, monitor->name, dst);
            error_count++;
        }
    }

    return error_count;
}

/**
 * Create a new listener for a service
 *
 * @param obj Listener configuration context
 *
 * @return Number of errors
 */
int create_new_listener(CONFIG_CONTEXT *obj)
{
    const char* protocol = config_get_string(obj->parameters, CN_PROTOCOL);
    mxb_assert(*protocol);

    if (const MXS_MODULE * mod = get_module(protocol, MODULE_PROTOCOL))
    {
        config_add_defaults(obj, config_listener_params);
        config_add_defaults(obj, mod->parameters);
    }
    else
    {
        MXS_ERROR("Unable to load protocol module '%s'.", protocol);
        return 1;
    }

    int error_count = 0;

    char *port = config_get_value(obj->parameters, CN_PORT);
    char *socket = config_get_value(obj->parameters, CN_SOCKET);

    if (socket && port)
    {
        MXS_ERROR("Creation of listener '%s' failed because both 'socket' and 'port' "
                  "are defined. Only one of them is allowed.", obj->object);
        error_count++;
    }
    else if (!socket && !port)
    {
        MXS_ERROR("Listener '%s' is missing a required parameter. A Listener "
                  "must have a service, protocol and port (or socket) defined.", obj->object);
        error_count++;
    }
    else
    {
        const char *address = config_get_string(obj->parameters, CN_ADDRESS);
        Service *service = static_cast<Service*>(config_get_service(obj->parameters, CN_SERVICE));
        mxb_assert(service);

        if (auto l = service_find_listener(service, socket, address, socket ? 0 : atoi(port)))
        {
            MXS_ERROR("Creation of listener '%s' for service '%s' failed, because "
                      "listener '%s' already listens on the %s %s.",
                      obj->object, service->name, l->name, socket ? "socket" : "port",
                      socket ? socket : port);
            return 1;
        }

        const char *protocol = config_get_string(obj->parameters, CN_PROTOCOL);
        SSL_LISTENER *ssl_info = NULL;

        if (!config_create_ssl(obj->object, obj->parameters, true, &ssl_info))
        {
            return 1;
        }

        // These two values being NULL trigger the loading of the default
        // authenticators that are specific to each protocol module
        char *authenticator = config_get_value(obj->parameters, CN_AUTHENTICATOR);
        char *authenticator_options = config_get_value(obj->parameters, CN_AUTHENTICATOR_OPTIONS);

        if (socket)
        {
            serviceCreateListener(service, obj->object, protocol, socket, 0,
                                  authenticator, authenticator_options, ssl_info);
        }
        else if (port)
        {
            serviceCreateListener(service, obj->object, protocol, address, atoi(port),
                                  authenticator, authenticator_options, ssl_info);
        }
    }

    return error_count;
}

/**
 * Create a new filter
 * @param obj Filter configuration context
 * @return Number of errors
 */
int create_new_filter(CONFIG_CONTEXT *obj)
{
    int error_count = 0;
    const char* module = config_get_value(obj->parameters, CN_MODULE);
    mxb_assert(*module);

    if (const MXS_MODULE* mod = get_module(module, MODULE_FILTER))
    {
        config_add_defaults(obj, mod->parameters);

        if (!filter_alloc(obj->object, module, obj->parameters))
        {
            MXS_ERROR("Failed to create filter '%s'. Memory allocation failed.",
                      obj->object);
            error_count++;
        }
    }
    else
    {
        MXS_ERROR("Failed to load filter module '%s'", module);
        error_count++;
    }

    return error_count;
}

bool config_have_required_ssl_params(CONFIG_CONTEXT *obj)
{
    MXS_CONFIG_PARAMETER *param = obj->parameters;

    return config_get_param(param, CN_SSL) &&
           config_get_param(param, CN_SSL_KEY) &&
           config_get_param(param, CN_SSL_CERT) &&
           config_get_param(param, CN_SSL_CA_CERT) &&
           strcmp(config_get_value_string(param, CN_SSL), CN_REQUIRED) == 0;
}

bool config_is_ssl_parameter(const char *key)
{
    const char *ssl_params[] =
    {
        CN_SSL_CERT,
        CN_SSL_CA_CERT,
        CN_SSL,
        CN_SSL_KEY,
        CN_SSL_VERSION,
        CN_SSL_CERT_VERIFY_DEPTH,
        CN_SSL_VERIFY_PEER_CERTIFICATE,
        NULL
    };

    for (int i = 0; ssl_params[i]; i++)
    {
        if (strcmp(key, ssl_params[i]) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool check_path_parameter(const MXS_MODULE_PARAM *params, const char *value)
{
    bool valid = false;

    if (params->options & (MXS_MODULE_OPT_PATH_W_OK |
                           MXS_MODULE_OPT_PATH_R_OK |
                           MXS_MODULE_OPT_PATH_X_OK |
                           MXS_MODULE_OPT_PATH_F_OK))
    {
        char buf[strlen(get_module_configdir()) + strlen(value) + 3];

        if (*value != '/')
        {
            sprintf(buf, "/%s/%s", get_module_configdir(), value);
            clean_up_pathname(buf);
        }
        else
        {
            strcpy(buf, value);
        }

        int mode = F_OK;
        int mask = X_OK;

        if (params->options & MXS_MODULE_OPT_PATH_W_OK)
        {
            mask |= S_IWUSR;
            mode |= W_OK;
        }
        if (params->options & MXS_MODULE_OPT_PATH_R_OK)
        {
            mask |= S_IRUSR | S_IRGRP | S_IROTH;
            mode |= R_OK;
        }
        if (params->options & MXS_MODULE_OPT_PATH_X_OK)
        {
            mask |= S_IXUSR | S_IXGRP | S_IXOTH;
        }

        if (access(buf, mode) == 0)
        {
            valid = true;
        }
        else
        {
            /** Save errno as we do a second call to `accept` */
            int er = errno;

            if (access(buf, F_OK) == 0 || (params->options & MXS_MODULE_OPT_PATH_CREAT) == 0)
            {
                /**
                 * Path already exists and it doesn't have the requested access
                 * right or the module doesn't want the directory to be created
                 * if it doesn't exist.
                 */
                MXS_ERROR("Bad path parameter '%s' (absolute path '%s'): %d, %s",
                          value, buf, er, mxs_strerror(er));
            }
            else if (mxs_mkdir_all(buf, mask))
            {
                /** Successfully created path */
                valid = true;
            }
            else
            {
                /** Failed to create the directory, errno is set in `mxs_mkdir_all` */
                MXS_ERROR("Can't create path '%s' (absolute path '%s'): %d, %s",
                          value, buf, errno, mxs_strerror(errno));
            }
        }
    }
    else
    {
        /** No checks for the path are required */
        valid = true;
    }

    return valid;
}

static bool config_contains_type(const CONFIG_CONTEXT *ctx, const char *name, const char *type)
{
    while (ctx)
    {
        if (strcmp(ctx->object, name) == 0 &&
            strcmp(type, config_get_value_string(ctx->parameters, CN_TYPE)) == 0)
        {
            return true;
        }

        ctx = ctx->next;
    }

    return false;
}

void fix_serverlist(char* value)
{
    string dest;
    char* end;
    char* start = strtok_r(value, ",", &end);
    const char* sep = "";

    while (start)
    {
        fix_object_name(start);
        dest += sep;
        dest += start;
        sep = ",";
        start = strtok_r(NULL, ",", &end);
    }

    /** The value will always be smaller than the original one or of equal size */
    strcpy(value, dest.c_str());
}

void config_fix_param(const MXS_MODULE_PARAM *params, MXS_CONFIG_PARAMETER *p)
{
    for (int i = 0; params[i].name; i++)
    {
        if (strcmp(params[i].name, p->name) == 0)
        {
            switch (params[i].type)
            {
            case MXS_MODULE_PARAM_SERVER:
            case MXS_MODULE_PARAM_SERVICE:
                fix_object_name(p->value);
                break;

            case MXS_MODULE_PARAM_SERVERLIST:
                fix_serverlist(p->value);
                break;

            case MXS_MODULE_PARAM_QUOTEDSTRING:
                // Remove *if* once '" .. "' is no longer optional
                if (check_first_last_char(p->value, '"'))
                {
                    remove_first_last_char(p->value);
                }
                break;

            case MXS_MODULE_PARAM_REGEX:
                // Remove *if* once '/ .. /' is no longer optional
                if (check_first_last_char(p->value, '/'))
                {
                    remove_first_last_char(p->value);
                }
                break;

            default:
                break;
            }

            break;
        }
    }
}

bool config_param_is_valid(const MXS_MODULE_PARAM *params, const char *key,
                           const char *value, const CONFIG_CONTEXT *context)
{
    bool valid = false;
    char fixed_value[strlen(value) + 1];
    strcpy(fixed_value, value);
    fix_object_name(fixed_value);

    for (int i = 0; params[i].name && !valid; i++)
    {
        if (strcmp(params[i].name, key) == 0)
        {
            char *endptr;

            switch (params[i].type)
            {
            case MXS_MODULE_PARAM_COUNT:
                if ((strtol(value, &endptr, 10)) >= 0 && endptr != value && *endptr == '\0')
                {
                    valid = true;
                }
                break;

            case MXS_MODULE_PARAM_INT:
                {
                    errno = 0;
                    long int v = strtol(value, &endptr, 10);
                    (void)v; // error: ignoring return value of 'strtol'
                    if ((errno == 0) && (endptr != value) && (*endptr == '\0'))
                    {
                        valid = true;
                    }
                }
                break;

            case MXS_MODULE_PARAM_SIZE:
                {
                    errno = 0;
                    long long int v = strtoll(value, &endptr, 10);
                    (void)v; // error: ignoring return value of 'strtoll'
                    if (errno == 0)
                    {
                        if (endptr != value)
                        {
                            switch (*endptr)
                            {
                            case 'T':
                            case 't':
                            case 'G':
                            case 'g':
                            case 'M':
                            case 'm':
                            case 'K':
                            case 'k':
                                if (*(endptr + 1) == '\0' ||
                                    ((*(endptr + 1) == 'i' || *(endptr + 1) == 'I') && *(endptr + 2) == '\0'))
                                {
                                    valid = true;
                                }
                                break;

                            case '\0':
                                valid = true;
                                break;

                            default:
                                break;
                            }
                        }
                    }
                }
                break;

            case MXS_MODULE_PARAM_BOOL:
                if (config_truth_value(value) != -1)
                {
                    valid = true;
                }
                break;

            case MXS_MODULE_PARAM_STRING:
                if (*value)
                {
                    valid = true;
                }
                break;

            case MXS_MODULE_PARAM_QUOTEDSTRING:
                if (*value)
                {
                    valid = true;
                    if (!check_first_last_char(value, '"'))
                    {
                        // Change warning to valid=false once quotes are no longer optional
                        MXS_WARNING("Missing quotes (\") around a quoted string is deprecated: '%s=%s'.",
                                    key, value);
                    }
                }
                break;

            case MXS_MODULE_PARAM_REGEX:
                valid = test_regex_string_validity(value, key);
                break;

            case MXS_MODULE_PARAM_ENUM:
                if (params[i].accepted_values)
                {
                    char *endptr;
                    const char *delim = ", \t";
                    char buf[strlen(value) + 1];
                    strcpy(buf, value);
                    char *tok = strtok_r(buf, delim, &endptr);

                    while (tok)
                    {
                        valid = false;

                        for (int j = 0; params[i].accepted_values[j].name; j++)
                        {
                            if (strcmp(params[i].accepted_values[j].name, tok) == 0)
                            {
                                valid = true;
                                break;
                            }
                        }

                        tok = strtok_r(NULL, delim, &endptr);

                        if ((params[i].options & MXS_MODULE_OPT_ENUM_UNIQUE) && (tok || !valid))
                        {
                            /** Either the only defined enum value is not valid
                             * or multiple values were defined */
                            valid = false;
                            break;
                        }
                    }
                }
                break;

            case MXS_MODULE_PARAM_SERVICE:
                if (context && config_contains_type(context, fixed_value, CN_SERVICE))
                {
                    valid = true;
                }
                break;

            case MXS_MODULE_PARAM_SERVER:
                if (context && config_contains_type(context, fixed_value, CN_SERVER))
                {
                    valid = true;
                }
                break;

            case MXS_MODULE_PARAM_SERVERLIST:
                if (context)
                {
                    valid = true;
                    char **server_names = NULL;
                    int n_serv = config_parse_server_list(value, &server_names);
                    if (n_serv > 0)
                    {
                        /* Check that every server name in the list is found in the config. */
                        for (int i = 0; i < n_serv; i++)
                        {
                            if (valid &&
                                !config_contains_type(context, server_names[i], CN_SERVER))
                            {
                                valid = false;
                            }
                            MXS_FREE(server_names[i]);
                        }
                        MXS_FREE(server_names);
                    }
                    break;
                }


            case MXS_MODULE_PARAM_PATH:
                valid = check_path_parameter(&params[i], value);
                break;

            default:
                MXS_ERROR("Unexpected module parameter type: %d", params[i].type);
                mxb_assert(false);
                break;
            }
        }
    }

    return valid;
}

int config_parse_server_list(const char *servers, char ***output_array)
{
    mxb_assert(servers);

    /* First, check the string for the maximum amount of servers it
     * might contain by counting the commas. */
    int out_arr_size = 1;
    const char *pos = servers;
    while ((pos = strchr(pos, ',')) != NULL)
    {
        pos++;
        out_arr_size++;
    }
    char **results = (char**)MXS_CALLOC(out_arr_size, sizeof(char*));
    if (!results)
    {
        return 0;
    }

    /* Parse the server names from the list. They are separated by ',' and will
     * be trimmed of whitespace. */
    char srv_list_tmp[strlen(servers) + 1];
    strcpy(srv_list_tmp, servers);
    trim(srv_list_tmp);

    bool error = false;
    int output_ind = 0;
    char *lasts;
    char *s = strtok_r(srv_list_tmp, ",", &lasts);
    while (s)
    {
        char srv_name_tmp[strlen(s) + 1];
        strcpy(srv_name_tmp, s);
        fix_object_name(srv_name_tmp);

        if (strlen(srv_name_tmp) > 0)
        {
            results[output_ind] = MXS_STRDUP(srv_name_tmp);
            if (!results[output_ind])
            {
                error = true;
                break;
            }
            output_ind++;
        }
        s = strtok_r(NULL, ",", &lasts);
    }

    if (error)
    {
        int i = 0;
        while (results[i])
        {
            MXS_FREE(results[i]);
            i++;
        }
        output_ind = 0;
    }

    if (output_ind == 0)
    {
        MXS_FREE(results);
    }
    else
    {
        *output_array = results;
    }
    return output_ind;
}

json_t* config_maxscale_to_json(const char* host)
{
    json_t* param = json_object();
    json_object_set_new(param, "libdir", json_string(get_libdir()));
    json_object_set_new(param, "datadir", json_string(get_datadir()));
    json_object_set_new(param, "process_datadir", json_string(get_process_datadir()));
    json_object_set_new(param, "cachedir", json_string(get_cachedir()));
    json_object_set_new(param, "configdir", json_string(get_configdir()));
    json_object_set_new(param, "config_persistdir", json_string(get_config_persistdir()));
    json_object_set_new(param, "module_configdir", json_string(get_module_configdir()));
    json_object_set_new(param, "piddir", json_string(get_piddir()));
    json_object_set_new(param, "logdir", json_string(get_logdir()));
    json_object_set_new(param, "langdir", json_string(get_langdir()));
    json_object_set_new(param, "execdir", json_string(get_execdir()));
    json_object_set_new(param, "connector_plugindir", json_string(get_connector_plugindir()));
    json_object_set_new(param, CN_THREADS, json_integer(config_threadcount()));
    json_object_set_new(param, CN_THREAD_STACK_SIZE, json_integer(config_thread_stack_size()));
    json_object_set_new(param, CN_WRITEQ_HIGH_WATER, json_integer(config_writeq_high_water()));
    json_object_set_new(param, CN_WRITEQ_LOW_WATER, json_integer(config_writeq_low_water()));

    MXS_CONFIG* cnf = config_get_global_options();

    json_object_set_new(param, CN_AUTH_CONNECT_TIMEOUT, json_integer(cnf->auth_conn_timeout));
    json_object_set_new(param, CN_AUTH_READ_TIMEOUT, json_integer(cnf->auth_read_timeout));
    json_object_set_new(param, CN_AUTH_WRITE_TIMEOUT, json_integer(cnf->auth_write_timeout));
    json_object_set_new(param, CN_SKIP_PERMISSION_CHECKS, json_boolean(cnf->skip_permission_checks));
    json_object_set_new(param, CN_ADMIN_AUTH, json_boolean(cnf->admin_auth));
    json_object_set_new(param, CN_ADMIN_ENABLED, json_boolean(cnf->admin_enabled));
    json_object_set_new(param, CN_ADMIN_LOG_AUTH_FAILURES, json_boolean(cnf->admin_log_auth_failures));
    json_object_set_new(param, CN_ADMIN_HOST, json_string(cnf->admin_host));
    json_object_set_new(param, CN_ADMIN_PORT, json_integer(cnf->admin_port));
    json_object_set_new(param, CN_ADMIN_SSL_KEY, json_string(cnf->admin_ssl_key));
    json_object_set_new(param, CN_ADMIN_SSL_CERT, json_string(cnf->admin_ssl_cert));
    json_object_set_new(param, CN_ADMIN_SSL_CA_CERT, json_string(cnf->admin_ssl_ca_cert));
    json_object_set_new(param, CN_PEER_HOSTS, json_string(cnf->peer_hosts));
    json_object_set_new(param, CN_PEER_USER, json_string(cnf->peer_user));
    json_object_set_new(param, CN_PASSIVE, json_boolean(cnf->passive));

    json_object_set_new(param, CN_QUERY_CLASSIFIER, json_string(cnf->qc_name));

    if (cnf->qc_args)
    {
        json_object_set_new(param, CN_QUERY_CLASSIFIER_ARGS, json_string(cnf->qc_args));
    }

    json_object_set_new(param, CN_QUERY_CLASSIFIER_CACHE_SIZE,
                        json_integer(cnf->qc_cache_properties.max_size));

    json_t* attr = json_object();
    time_t started = maxscale_started();
    time_t activated = started + MXS_CLOCK_TO_SEC(cnf->promoted_at);
    json_object_set_new(attr, CN_PARAMETERS, param);
    json_object_set_new(attr, "version", json_string(MAXSCALE_VERSION));
    json_object_set_new(attr, "commit", json_string(MAXSCALE_COMMIT));
    json_object_set_new(attr, "started_at", json_string(http_to_date(started).c_str()));
    json_object_set_new(attr, "activated_at", json_string(http_to_date(activated).c_str()));
    json_object_set_new(attr, "uptime", json_integer(maxscale_uptime()));

    json_t* obj = json_object();
    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_object_set_new(obj, CN_ID, json_string(CN_MAXSCALE));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MAXSCALE));

    return mxs_json_resource(host, MXS_JSON_API_MAXSCALE, obj);
}

/**
 * Creates a global configuration at the location pointed by @c filename
 *
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
static bool create_global_config(const char *filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing global configuration: %d, %s",
                  filename, errno, mxs_strerror(errno));
        return false;
    }

    dprintf(file, "[maxscale]\n");
    dprintf(file, "%s=%u\n", CN_AUTH_CONNECT_TIMEOUT, gateway.auth_conn_timeout);
    dprintf(file, "%s=%u\n", CN_AUTH_READ_TIMEOUT, gateway.auth_read_timeout);
    dprintf(file, "%s=%u\n", CN_AUTH_WRITE_TIMEOUT, gateway.auth_write_timeout);
    dprintf(file, "%s=%s\n", CN_ADMIN_AUTH, gateway.admin_auth ? "true" : "false");
    dprintf(file, "%s=%u\n", CN_PASSIVE, gateway.passive);

    close(file);

    return true;
}

bool config_global_serialize()
{
    static const char* GLOBAL_CONFIG_NAME = "global-options";
    bool rval = false;
    char filename[PATH_MAX];

    snprintf(filename, sizeof(filename), "%s/%s.cnf.tmp", get_config_persistdir(),
             GLOBAL_CONFIG_NAME);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary global configuration at '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }
    else if (create_global_config(filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char *dot = strrchr(final_filename, '.');
        mxb_assert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary server configuration at '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
    }

    return rval;
}

/**
 * Test if first and last char in the string are as expected.
 *
 * @param string Input string
 * @param expected Required character
 * @return True, if string has at least two chars and both first and last char
 * equal @c expected
 */
static bool check_first_last_char(const char* string, char expected)
{
    bool valid = false;
    {
        size_t len = strlen(string);
        if ((len >= 2) && (string[0] == expected) && (string[len - 1] == expected))
        {
            valid = true;
        }
    }
    return valid;
}

/**
 * Chop a char off from both ends of the string.
 *
 * @param value Input string
 */
static void remove_first_last_char(char* value)
{
    size_t len = strlen(value);
    value[len - 1] = '\0';
    memmove(value, value + 1, len - 1);
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
static pcre2_code* compile_regex_string(const char* regex_string, bool jit_enabled,
                                        uint32_t options, uint32_t* output_ovector_size)
{
    bool success = true;
    int errorcode = -1;
    PCRE2_SIZE error_offset = -1;
    uint32_t capcount = 0;
    pcre2_code* machine =
        pcre2_compile((PCRE2_SPTR) regex_string, PCRE2_ZERO_TERMINATED, options,
                      &errorcode, &error_offset, NULL);
    if (machine)
    {
        if (jit_enabled)
        {
            // Try to compile even further for faster matching
            if (pcre2_jit_compile(machine, PCRE2_JIT_COMPLETE) < 0)
            {
                MXS_WARNING("PCRE2 JIT compilation of pattern '%s' failed, "
                            "falling back to normal compilation.", regex_string);
            }
        }
        /* Check what is the required match_data size for this pattern. */
        int ret_info = pcre2_pattern_info(machine, PCRE2_INFO_CAPTURECOUNT, &capcount);
        if (ret_info != 0)
        {
            MXS_PCRE2_PRINT_ERROR(ret_info);
            success = false;
        }
    }
    else
    {
        MXS_ERROR("Invalid PCRE2 regular expression '%s' (position '%zu').",
                  regex_string, error_offset);
        MXS_PCRE2_PRINT_ERROR(errorcode);
        success = false;
    }

    if (!success)
    {
        pcre2_code_free(machine);
        machine = NULL;
    }
    else if (output_ovector_size)
    {
        *output_ovector_size = capcount + 1;
    }
    return machine;
}

/**
 * Test if the given string is a valid MaxScale regular expression and can be
 * compiled to a regex machine using PCRE2.
 *
 * @param regex_string The input string
 * @return True if compilation succeeded, false if string is invalid or cannot
 * be compiled.
 */
static bool test_regex_string_validity(const char* regex_string, const char* key)
{
    if (*regex_string == '\0')
    {
        return false;
    }
    char regex_copy[strlen(regex_string) + 1];
    strcpy(regex_copy, regex_string);
    if (!check_first_last_char(regex_string, '/'))
    {
        //return false; // Uncomment this line once '/ .. /' is no longer optional
        MXS_WARNING("Missing slashes (/) around a regular expression is deprecated: '%s=%s'.",
                    key, regex_string);
    }
    else
    {
        remove_first_last_char(regex_copy);
    }

    pcre2_code* code = compile_regex_string(regex_copy, false, 0, NULL);
    bool rval = (code != NULL);
    pcre2_code_free(code);
    return rval;
}

bool get_suffixed_size(const char* value, uint64_t* dest)
{
    if (!isdigit(*value))
    {
        // This will also catch negative values
        return false;
    }

    bool rval = false;
    char *end;
    uint64_t size = strtoll(value, &end, 10);

    switch (*end)
    {
    case 'T':
    case 't':
        if ((*(end + 1) == 'i') || (*(end + 1) == 'I'))
        {
            size *= 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        }
        else
        {
            size *= 1000ULL * 1000ULL * 1000ULL * 1000ULL;
        }
        break;

    case 'G':
    case 'g':
        if ((*(end + 1) == 'i') || (*(end + 1) == 'I'))
        {
            size *= 1024ULL * 1024ULL * 1024ULL;
        }
        else
        {
            size *= 1000ULL * 1000ULL * 1000ULL;
        }
        break;

    case 'M':
    case 'm':
        if ((*(end + 1) == 'i') || (*(end + 1) == 'I'))
        {
            size *= 1024ULL * 1024ULL;
        }
        else
        {
            size *= 1000ULL * 1000ULL;
        }
        break;

    case 'K':
    case 'k':
        if ((*(end + 1) == 'i') || (*(end + 1) == 'I'))
        {
            size *= 1024ULL;
        }
        else
        {
            size *= 1000ULL;
        }
        break;

    default:
        break;
    }

    const std::set<char> first{ 'T', 't', 'G', 'g', 'M', 'm', 'K', 'k' };
    const std::set<char> second{ 'I', 'i' };

    if (end[0] == '\0')
    {
        rval = true;
    }
    else if (end[1] == '\0')
    {
        // First character must be valid
        rval = first.count(end[0]);
    }
    else if (end[2] == '\0')
    {
        // Both characters have to be valid
        rval = first.count(end[0]) && second.count(end[1]);
    }

    if (dest)
    {
        *dest = size;
    }

    return rval;
}

bool config_parse_disk_space_threshold(MxsDiskSpaceThreshold* pDisk_space_threshold,
                                     const char* zDisk_space_threshold)
{
    mxb_assert(pDisk_space_threshold);
    mxb_assert(zDisk_space_threshold);

    bool success = true;

    using namespace std;

    MxsDiskSpaceThreshold disk_space_threshold;
    string s(zDisk_space_threshold);

    // Somewhat simplified, this is what we expect: [^:]+:[:digit:]+(,[^:]+:[:digit:]+)*
    // So, e.g. the following are fine "/data:20", "/data1:50,/data2:60", "*:80".

    while (success && !s.empty())
    {
        size_t i = s.find_first_of(',');
        string entry = s.substr(0, i);

        s.erase(0, i != string::npos ? i + 1 : i);

        size_t j = entry.find_first_of(':');

        if (j != string::npos)
        {
            string path = entry.substr(0, j);
            string tail = entry.substr(j + 1);

            mxs::trim(path);
            mxs::trim(tail);

            if (!path.empty() && !tail.empty())
            {
                char* end;
                int32_t percentage = strtol(tail.c_str(), &end, 0);

                if ((*end == 0) && (percentage >= 0) && (percentage <= 100))
                {
                    disk_space_threshold[path] = percentage;
                }
                else
                {
                    MXS_ERROR("The value following the ':' must be a percentage: %s",
                              entry.c_str());
                    success = false;
                }
            }
            else
            {
                MXS_ERROR("The %s parameter '%s' contains an invalid entry: '%s'",
                          CN_DISK_SPACE_THRESHOLD, zDisk_space_threshold, entry.c_str());
                success = false;
            }
        }
        else
        {
            MXS_ERROR("The %s parameter '%s' contains an invalid entry: '%s'",
                      CN_DISK_SPACE_THRESHOLD, zDisk_space_threshold, entry.c_str());
            success = false;
        }
    }

    if (success)
    {
        pDisk_space_threshold->swap(disk_space_threshold);
    }

    return success;
}

namespace maxscale
{

ParamList::ParamList(std::initializer_list<std::pair<const char*, const char*>> list,
                     const MXS_MODULE_PARAM* module_params)
{
    for (const auto& a : list)
    {
        config_add_param(&m_ctx, a.first, a.second);
    }

    if (module_params)
    {
        config_add_defaults(&m_ctx, module_params);
    }
}

ParamList::~ParamList()
{
    config_parameter_free(m_ctx.parameters);
}

MXS_CONFIG_PARAMETER* ParamList::params()
{
    return m_ctx.parameters;
}

}

void dump_if_changed(const MXS_MODULE_PARAM* params, int file,
                     const std::string& key, const std::string& value)
{
    for (int i = 0; params[i].name; i++)
    {
        if (params[i].name == key)
        {
            /**
             * This detects only exact matches, not ones that are logically equivalent
             * but lexicographically different e.g. 1 and true. This might not
             * be a bad thing: it'll distinct user defined values from defaults.
             */

            if (!params[i].default_value || value != params[i].default_value)
            {
                if (dprintf(file, "%s=%s\n", key.c_str(), value.c_str()) == -1)
                {
                    MXS_ERROR("Failed to serialize service value: %d, %s",
                              errno, mxs_strerror(errno));
                }
            }

            break;
        }
    }
}

void dump_param_list(int file, MXS_CONFIG_PARAMETER* list,
                     const std::unordered_set<std::string>& ignored,
                     const MXS_MODULE_PARAM* common_params,
                     const MXS_MODULE_PARAM* module_params)
{
    for (auto p = list; p; p = p->next)
    {
        if (ignored.count(p->name) == 0 && *p->value)
        {
            dump_if_changed(common_params, file, p->name, p->value);
            dump_if_changed(module_params, file, p->name, p->value);
        }
    }
}
