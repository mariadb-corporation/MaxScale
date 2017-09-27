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

/**
 * @file config.c Configuration file processing
 */

#include <maxscale/config.h>

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
#include <set>
#include <string>

#include <maxscale/adminusers.h>
#include <maxscale/alloc.h>
#include <maxscale/housekeeper.h>
#include <maxscale/limits.h>
#include <maxscale/log_manager.h>
#include <maxscale/pcre2.h>
#include <maxscale/spinlock.h>
#include <maxscale/utils.h>
#include <maxscale/paths.h>
#include <maxscale/json_api.h>
#include <maxscale/http.hh>
#include <maxscale/version.h>
#include <maxscale/maxscale.h>
#include <maxscale/hk_heartbeat.h>

#include "maxscale/config.h"
#include "maxscale/filter.h"
#include "maxscale/service.h"
#include "maxscale/monitor.h"
#include "maxscale/modules.h"
#include "maxscale/router.h"

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
const char CN_AUTHENTICATOR_OPTIONS[]         = "authenticator_options";
const char CN_AUTH_ALL_SERVERS[]              = "auth_all_servers";
const char CN_AUTH_CONNECT_TIMEOUT[]          = "auth_connect_timeout";
const char CN_AUTH_READ_TIMEOUT[]             = "auth_read_timeout";
const char CN_AUTH_WRITE_TIMEOUT[]            = "auth_write_timeout";
const char CN_AUTO[]                          = "auto";
const char CN_CONNECTION_TIMEOUT[]            = "connection_timeout";
const char CN_DATA[]                          = "data";
const char CN_DEFAULT[]                       = "default";
const char CN_DESCRIPTION[]                   = "description";
const char CN_ENABLE_ROOT_USER[]              = "enable_root_user";
const char CN_FILTERS[]                       = "filters";
const char CN_FILTER[]                        = "filter";
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
const char CN_MS_TIMESTAMP[]                  = "ms_timestamp";
const char CN_NAME[]                          = "name";
const char CN_NON_BLOCKING_POLLS[]            = "non_blocking_polls";
const char CN_OPTIONS[]                       = "options";
const char CN_PARAMETERS[]                    = "parameters";
const char CN_PASSIVE[]                       = "passive";
const char CN_PASSWORD[]                      = "password";
const char CN_POLL_SLEEP[]                    = "poll_sleep";
const char CN_PORT[]                          = "port";
const char CN_PROTOCOL[]                      = "protocol";
const char CN_QUERY_CLASSIFIER[]              = "query_classifier";
const char CN_QUERY_CLASSIFIER_ARGS[]         = "query_classifier_args";
const char CN_RELATIONSHIPS[]                 = "relationships";
const char CN_LINKS[]                         = "links";
const char CN_REQUIRED[]                      = "required";
const char CN_RETRY_ON_FAILURE[]              = "retry_on_failure";
const char CN_ROUTER[]                        = "router";
const char CN_ROUTER_OPTIONS[]                = "router_options";
const char CN_SELF[]                          = "self";
const char CN_SERVERS[]                       = "servers";
const char CN_SERVER[]                        = "server";
const char CN_SERVICES[]                      = "services";
const char CN_SERVICE[]                       = "service";
const char CN_SESSIONS[]                      = "sessions";
const char CN_SKIP_PERMISSION_CHECKS[]        = "skip_permission_checks";
const char CN_SOCKET[]                        = "socket";
const char CN_SQL_MODE[]                      = "sql_mode";
const char CN_STATE[]                         = "state";
const char CN_SSL[]                           = "ssl";
const char CN_SSL_CA_CERT[]                   = "ssl_ca_cert";
const char CN_SSL_CERT[]                      = "ssl_cert";
const char CN_SSL_CERT_VERIFY_DEPTH[]         = "ssl_cert_verify_depth";
const char CN_SSL_KEY[]                       = "ssl_key";
const char CN_SSL_VERSION[]                   = "ssl_version";
const char CN_STRIP_DB_ESC[]                  = "strip_db_esc";
const char CN_THREADS[]                       = "threads";
const char CN_THREAD_STACK_SIZE[]             = "thread_stack_size";
const char CN_TYPE[]                          = "type";
const char CN_UNIX[]                          = "unix";
const char CN_USER[]                          = "user";
const char CN_USERS[]                         = "users";
const char CN_VERSION_STRING[]                = "version_string";
const char CN_WEIGHTBY[]                      = "weightby";

typedef struct duplicate_context
{
    HASHTABLE        *hash;
    pcre2_code       *re;
    pcre2_match_data *mdata;
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
static uint64_t get_suffixed_size(const char* value);

int config_get_ifaddr(unsigned char *output);
static int config_get_release_string(char* release);
bool config_has_duplicate_sections(const char* config, DUPLICATE_CONTEXT* context);
int create_new_service(CONFIG_CONTEXT *obj);
int create_new_server(CONFIG_CONTEXT *obj);
int create_new_monitor(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj, HASHTABLE* monitorhash);
int create_new_listener(CONFIG_CONTEXT *obj);
int create_new_filter(CONFIG_CONTEXT *obj);
int configure_new_service(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj);
void config_fix_param(const MXS_MODULE_PARAM *params, MXS_CONFIG_PARAMETER *p);

static const char *config_file = NULL;
static MXS_CONFIG gateway;
char *version_string = NULL;
static bool is_persisted_config = false; /**< True if a persisted configuration file is being parsed */

const char *config_service_params[] =
{
    CN_TYPE,
    CN_ROUTER,
    CN_ROUTER_OPTIONS,
    CN_SERVERS,
    CN_MONITOR,
    CN_USER,
    "passwd", // DEPRECATE: See config_get_password.
    CN_PASSWORD,
    CN_ENABLE_ROOT_USER,
    CN_MAX_RETRY_INTERVAL,
    CN_MAX_CONNECTIONS,
    "max_queued_connections", //TODO: Fix this
    "queued_connection_timeout", // TODO: Fix this
    CN_CONNECTION_TIMEOUT,
    CN_AUTH_ALL_SERVERS,
    CN_STRIP_DB_ESC,
    CN_LOCALHOST_MATCH_WILDCARD_HOST,
    CN_VERSION_STRING,
    CN_FILTERS,
    CN_WEIGHTBY,
    CN_LOG_AUTH_WARNINGS,
    CN_RETRY_ON_FAILURE,
    NULL
};

const char *config_listener_params[] =
{
    CN_AUTHENTICATOR_OPTIONS,
    CN_TYPE,
    CN_SERVICE,
    CN_PROTOCOL,
    CN_PORT,
    CN_ADDRESS,
    CN_SOCKET,
    CN_AUTHENTICATOR,
    CN_SSL_CERT,
    CN_SSL_CA_CERT,
    CN_SSL,
    CN_SSL_KEY,
    CN_SSL_VERSION,
    CN_SSL_CERT_VERIFY_DEPTH,
    NULL
};

const char *config_monitor_params[] =
{
    CN_TYPE,
    CN_MODULE,
    CN_SERVERS,
    CN_USER,
    "passwd",   // DEPRECATE: See config_get_password.
    CN_PASSWORD,
    CN_SCRIPT,
    CN_EVENTS,
    CN_MONITOR_INTERVAL,
    CN_JOURNAL_MAX_AGE,
    CN_SCRIPT_TIMEOUT,
    CN_FAILOVER,
    CN_FAILOVER_TIMEOUT,
    CN_BACKEND_CONNECT_TIMEOUT,
    CN_BACKEND_READ_TIMEOUT,
    CN_BACKEND_WRITE_TIMEOUT,
    CN_BACKEND_CONNECT_ATTEMPTS,
    NULL
};

const char *config_filter_params[] =
{
    CN_TYPE,
    CN_MODULE,
    NULL
};

const char *server_params[] =
{
    CN_TYPE,
    CN_PROTOCOL,
    CN_PORT,
    CN_ADDRESS,
    CN_AUTHENTICATOR,
    CN_AUTHENTICATOR_OPTIONS,
    CN_MONITORUSER,
    CN_MONITORPW,
    CN_PERSISTPOOLMAX,
    CN_PERSISTMAXTIME,
    CN_SSL_CERT,
    CN_SSL_CA_CERT,
    CN_SSL,
    CN_SSL_KEY,
    CN_SSL_VERSION,
    CN_SSL_CERT_VERIFY_DEPTH,
    CN_PROXY_PROTOCOL,
    NULL
};

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
    HASHTABLE *hash = hashtable_alloc(table_size, hashtable_item_strhash, hashtable_item_strcmp);
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR) "^\\s*\\[(.+)\\]\\s*$", PCRE2_ZERO_TERMINATED,
                                   0, &errcode, &erroffset, NULL);
    pcre2_match_data *mdata = NULL;

    if (hash && re && (mdata = pcre2_match_data_create_from_pattern(re, NULL)))
    {
        hashtable_memory_fns(hash, hashtable_item_strdup, NULL, hashtable_item_free, NULL);

        context->hash = hash;
        context->re = re;
        context->mdata = mdata;
        rv = true;
    }
    else
    {
        pcre2_match_data_free(mdata);
        pcre2_code_free(re);
        hashtable_free(hash);
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
    hashtable_free(context->hash);

    context->mdata = NULL;
    context->re = NULL;
    context->hash = NULL;
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
        ctx->element = NULL;
    }

    return ctx;
}

/** A set that holds all the section names that contain whitespace */
static std::set<string> warned_whitespace;

/**
 * @brief Fix section names
 *
 * Check that section names contain no whitespace. If the name contains
 * whitespace, trim it, squeeze it and replace the remainig whitespace with
 * hyphens. If a replacement was made, a warning is logged.
 *
 * @param section Section name
 */
void fix_section_name(char *section)
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

    squeeze_whitespace(section);
    trim(section);
    replace_whitespace(section);
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
static int
ini_handler(void *userdata, const char *section, const char *name, const char *value)
{
    CONFIG_CONTEXT   *cntxt = (CONFIG_CONTEXT *)userdata;
    CONFIG_CONTEXT   *ptr = cntxt;

    if (strcmp(section, CN_GATEWAY) == 0 || strcasecmp(section, CN_MAXSCALE) == 0)
    {
        return handle_global_item(name, value);
    }
    else if (strlen(section) == 0)
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

    return 1;
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

    if (typeflag == FTW_F) // We are only interested in files,
    {
        const char* filename = fpath + ftwbuf->base;
        const char* dot = strrchr(filename, '.');

        if (dot) // that must have a suffix,
        {
            const char* suffix = dot + 1;

            if (strcmp(suffix, "cnf") == 0) // that is ".cnf".
            {
                ss_dassert(current_dcontext);
                ss_dassert(current_ccontext);

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
        ss_dassert(rc == GLOB_NOMATCH);
        break;
    }

    globfree(&matches);

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
        CONFIG_CONTEXT ccontext = {};
        ccontext.object = (char*)"";

        if (config_load_single_file(filename, &dcontext, &ccontext))
        {
            const char DIR_SUFFIX[] = ".d";

            char dir[strlen(filename) + sizeof(DIR_SUFFIX)];
            strcpy(dir, filename);
            strcat(dir, DIR_SUFFIX);

            rval = true;

            if (is_directory(dir))
            {
                rval = config_load_dir(dir, &dcontext, &ccontext);
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
                    rval = config_load_dir(persist_cnf, &p_dcontext, &ccontext);
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
                if (!check_config_objects(ccontext.next) || !process_config(ccontext.next))
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

        config_context_free(ccontext.next);

        duplicate_context_finish(&dcontext);
    }
    return rval;
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
    ss_dassert(!config_file);

    config_file = filename;
    bool rval = config_load_and_process(filename, process_config_context);

    return rval;
}

/**
 * Reload the configuration file for the MaxScale
 *
 * @return True on success, false on fatal error.
 */
bool config_reload()
{
    bool rval = false;

    if (config_file)
    {
        if (gateway.version_string)
        {
            MXS_FREE(gateway.version_string);
        }

        rval = config_load_and_process(config_file, process_config_update);
    }
    else
    {
        MXS_ERROR("config_reload() called without the configuration having "
                  "been loaded first.");
    }

    return rval;
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
    CONFIG_CONTEXT  *obj;
    int             error_count = 0;
    HASHTABLE*      monitorhash;

    if ((monitorhash = hashtable_alloc(5, hashtable_item_strhash, hashtable_item_strcmp)) == NULL)
    {
        MXS_ERROR("Failed to allocate, monitor configuration check hashtable.");
        return 0;
    }
    hashtable_memory_fns(monitorhash, hashtable_item_strdup, NULL, hashtable_item_free, NULL);

    /**
     * Process the data and create the services and servers defined
     * in the data.
     */
    obj = context;
    while (obj)
    {
        char *type = config_get_value(obj->parameters, CN_TYPE);
        if (type)
        {
            if (!strcmp(type, CN_SERVICE))
            {
                error_count += create_new_service(obj);
            }
            else if (!strcmp(type, "server"))
            {
                error_count += create_new_server(obj);
            }
            else if (!strcmp(type, "filter"))
            {
                error_count += create_new_filter(obj);
            }
        }
        else
        {
            MXS_ERROR("Configuration object '%s' has no type.", obj->object);
            error_count++;
        }
        obj = obj->next;
    }

    if (error_count == 0)
    {
        /*
         * Now we have created the services, servers and filters and we can add the
         * servers and filters to the services. Monitors are also created at this point
         * because they require a set of servers to monitor.
         */
        obj = context;
        while (obj)
        {
            char *type = config_get_value(obj->parameters, CN_TYPE);
            if (type)
            {
                if (!strcmp(type, CN_SERVICE))
                {
                    error_count += configure_new_service(context, obj);
                }
                else if (!strcmp(type, CN_LISTENER))
                {
                    error_count += create_new_listener(obj);
                }
                else if (!strcmp(type, CN_MONITOR))
                {
                    error_count += create_new_monitor(context, obj, monitorhash);
                }
                else if (strcmp(type, CN_SERVER) != 0 && strcmp(type, CN_FILTER) != 0)
                {
                    MXS_ERROR("Configuration object '%s' has an invalid type specified.",
                              obj->object);
                    error_count++;
                }
            }
            obj = obj->next;
        }
    }
    /** TODO: consistency check function */

    hashtable_free(monitorhash);
    /**
     * error_count += consistency_checks();
     */

#ifdef REQUIRE_LISTENERS
    if (!service_all_services_have_listeners())
    {
        error_count++;
    }
#endif

    if (error_count)
    {
        MXS_ERROR("%d errors were encountered while processing the configuration "
                  "file '%s'.", error_count, config_file);
    }

    return error_count == 0;
}

/**
 * Get the value of a config parameter
 *
 * @param params        The linked list of config parameters
 * @param name          The parameter to return
 * @return the parameter value or NULL if not found
 */
static char *
config_get_value(MXS_CONFIG_PARAMETER *params, const char *name)
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

    return get_suffixed_size(value);
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
                ss_dassert(found == res_ind);
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
        ss_dassert(out_codes[i]);
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

/**
 * Free a configuration parameter
 * @param p1 Parameter to free
 */
void config_parameter_free(MXS_CONFIG_PARAMETER* p1)
{
    while (p1)
    {
        MXS_FREE(p1->name);
        MXS_FREE(p1->value);
        MXS_CONFIG_PARAMETER* p2 = p1->next;
        MXS_FREE(p1);
        p1 = p2;
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
    int i;
    if (strcmp(name, CN_THREADS) == 0)
    {
        if (strcmp(value, CN_AUTO) == 0)
        {
            if ((gateway.n_threads = get_processor_count()) > 1)
            {
                gateway.n_threads--;
            }
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

        if (gateway.n_threads > MXS_MAX_THREADS)
        {
            MXS_WARNING("Number of threads set to %d, which is greater than the "
                        "hard maximum of %d. Number of threads adjusted down "
                        "accordingly.", gateway.n_threads, MXS_MAX_THREADS);
            gateway.n_threads = MXS_MAX_THREADS;
        }
    }
    else if (strcmp(name, CN_THREAD_STACK_SIZE) == 0)
    {
        gateway.thread_stack_size = get_suffixed_size(value);
    }
    else if (strcmp(name, CN_NON_BLOCKING_POLLS) == 0)
    {
        gateway.n_nbpoll = atoi(value);
    }
    else if (strcmp(name, CN_POLL_SLEEP) == 0)
    {
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
    else
    {
        for (i = 0; lognames[i].name; i++)
        {
            if (strcasecmp(name, lognames[i].name) == 0)
            {
                if (lognames[i].replacement)
                {
                    MXS_WARNING("In the configuration file the use of '%s' is deprecated, "
                                "use '%s' instead.",
                                lognames[i].name, lognames[i].replacement);
                }

                mxs_log_set_priority_enabled(lognames[i].priority, config_truth_value(value));
            }
        }
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

/**
 * Form an SSL structure from listener section parameters
 *
 * @param obj The configuration object for the item being created
 * @param require_cert  Whether a certificate and key are required
 * @param *error_count  An error count which may be incremented
 * @return SSL_LISTENER structure or NULL
 */
SSL_LISTENER* make_ssl_structure (CONFIG_CONTEXT *obj, bool require_cert, int *error_count)
{
    char *ssl, *ssl_version, *ssl_cert, *ssl_key, *ssl_ca_cert, *ssl_cert_verify_depth;
    int local_errors = 0;
    SSL_LISTENER *new_ssl;

    ssl = config_get_value(obj->parameters, CN_SSL);

    if (ssl)
    {
        if (!strcmp(ssl, CN_REQUIRED))
        {
            if ((new_ssl = (SSL_LISTENER*)MXS_CALLOC(1, sizeof(SSL_LISTENER))) == NULL)
            {
                return NULL;
            }
            new_ssl->ssl_method_type = SERVICE_SSL_TLS_MAX;
            ssl_cert = config_get_value(obj->parameters, CN_SSL_CERT);
            ssl_key = config_get_value(obj->parameters, CN_SSL_KEY);
            ssl_ca_cert = config_get_value(obj->parameters, CN_SSL_CA_CERT);
            ssl_version = config_get_value(obj->parameters, CN_SSL_VERSION);
            ssl_cert_verify_depth = config_get_value(obj->parameters, CN_SSL_CERT_VERIFY_DEPTH);
            new_ssl->ssl_init_done = false;

            if (ssl_version)
            {
                if (listener_set_ssl_version(new_ssl, ssl_version) != 0)
                {
                    MXS_ERROR("Unknown parameter value for 'ssl_version' for"
                              " service '%s': %s", obj->object, ssl_version);
                    local_errors++;
                }
            }

            if (ssl_cert_verify_depth)
            {
                new_ssl->ssl_cert_verify_depth = atoi(ssl_cert_verify_depth);
                if (new_ssl->ssl_cert_verify_depth < 0)
                {
                    MXS_ERROR("Invalid parameter value for 'ssl_cert_verify_depth"
                              " for service '%s': %s", obj->object, ssl_cert_verify_depth);
                    new_ssl->ssl_cert_verify_depth = 0;
                    local_errors++;
                }
            }
            else
            {
                /**
                 * Default of 9 as per Linux man page
                 */
                new_ssl->ssl_cert_verify_depth = 9;
            }

            listener_set_certificates(new_ssl, ssl_cert, ssl_key, ssl_ca_cert);

            if (require_cert && new_ssl->ssl_cert == NULL)
            {
                local_errors++;
                MXS_ERROR("Server certificate missing for service '%s'."
                          "Please provide the path to the server certificate by adding "
                          "the ssl_cert=<path> parameter", obj->object);
            }

            if (require_cert && new_ssl->ssl_ca_cert == NULL)
            {
                local_errors++;
                MXS_ERROR("CA Certificate missing for service '%s'."
                          "Please provide the path to the certificate authority "
                          "certificate by adding the ssl_ca_cert=<path> parameter",
                          obj->object);
            }

            if (require_cert && new_ssl->ssl_key == NULL)
            {
                local_errors++;
                MXS_ERROR("Server private key missing for service '%s'. "
                          "Please provide the path to the server certificate key by "
                          "adding the ssl_key=<path> parameter",
                          obj->object);
            }

            if (require_cert && access(new_ssl->ssl_ca_cert, F_OK) != 0)
            {
                MXS_ERROR("Certificate authority file for service '%s' not found: %s",
                          obj->object,
                          new_ssl->ssl_ca_cert);
                local_errors++;
            }

            if (require_cert && access(new_ssl->ssl_cert, F_OK) != 0)
            {
                MXS_ERROR("Server certificate file for service '%s' not found: %s",
                          obj->object,
                          new_ssl->ssl_cert);
                local_errors++;
            }

            if (require_cert && access(new_ssl->ssl_key, F_OK) != 0)
            {
                MXS_ERROR("Server private key file for service '%s' not found: %s",
                          obj->object,
                          new_ssl->ssl_key);
                local_errors++;
            }

            if (0 == local_errors)
            {
                return new_ssl;
            }
            *error_count += local_errors;
            MXS_FREE(new_ssl);
        }
        else if (strcmp(ssl, "disabled") != 0)
        {
            MXS_ERROR("Unknown value for 'ssl': %s. Service will not use SSL.", ssl);
        }
    }
    return NULL;
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
    gateway.log_to_shm = 0;
    gateway.admin_port = DEFAULT_ADMIN_HTTP_PORT;
    gateway.admin_auth = true;
    gateway.admin_log_auth_failures = true;
    gateway.admin_enabled = true;
    strcpy(gateway.admin_host, DEFAULT_ADMIN_HOST);
    gateway.admin_ssl_key[0] = '\0';
    gateway.admin_ssl_cert[0] = '\0';
    gateway.admin_ssl_ca_cert[0] = '\0';
    gateway.passive = false;
    gateway.promoted_at = 0;

    gateway.thread_stack_size = 0;
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
    gateway.id = 0;

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
 * Process a configuration context update and turn it into the set of object
 * we need.
 *
 * @param context       The configuration data
 */
static bool
process_config_update(CONFIG_CONTEXT *context)
{
    CONFIG_CONTEXT *obj;
    SERVICE        *service;
    SERVER         *server;

    /**
     * Process the data and create the services and servers defined
     * in the data.
     */
    obj = context;
    while (obj)
    {
        char *type = config_get_value(obj->parameters, CN_TYPE);
        if (type == NULL)
        {
            MXS_ERROR("Configuration object %s has no type.", obj->object);
        }
        else if (!strcmp(type, CN_SERVICE))
        {
            char *router = config_get_value(obj->parameters, CN_ROUTER);
            if (router)
            {
                if ((service = service_find(obj->object)) != NULL)
                {
                    char *user;
                    char *auth;
                    char *enable_root_user;

                    const char *max_connections;
                    const char *max_queued_connections;
                    const char *queued_connection_timeout;
                    char *connection_timeout;

                    char* auth_all_servers;
                    char* strip_db_esc;
                    char* max_slave_conn_str;
                    char* max_slave_rlag_str;
                    char *version_string;
                    char *allow_localhost_match_wildcard_host;

                    enable_root_user = config_get_value(obj->parameters, CN_ENABLE_ROOT_USER);

                    connection_timeout = config_get_value(obj->parameters, CN_CONNECTION_TIMEOUT);
                    max_connections = config_get_value_string(obj->parameters, CN_MAX_CONNECTIONS);
                    max_queued_connections = config_get_value_string(obj->parameters, "max_queued_connections");
                    queued_connection_timeout = config_get_value_string(obj->parameters, "queued_connection_timeout");
                    user = config_get_value(obj->parameters, CN_USER);
                    auth = config_get_password(obj->parameters);

                    auth_all_servers = config_get_value(obj->parameters, CN_AUTH_ALL_SERVERS);
                    strip_db_esc = config_get_value(obj->parameters, CN_STRIP_DB_ESC);
                    version_string = config_get_value(obj->parameters, CN_VERSION_STRING);
                    allow_localhost_match_wildcard_host =
                        config_get_value(obj->parameters, CN_LOCALHOST_MATCH_WILDCARD_HOST);

                    char *log_auth_warnings = config_get_value(obj->parameters, CN_LOG_AUTH_WARNINGS);
                    int truthval;
                    if (log_auth_warnings && (truthval = config_truth_value(log_auth_warnings)) != -1)
                    {
                        service->log_auth_warnings = (bool)truthval;
                    }

                    if (version_string)
                    {
                        serviceSetVersionString(service, version_string);
                    }

                    if (user && auth)
                    {
                        service_update(service, router, user, auth);
                        if (enable_root_user)
                        {
                            serviceEnableRootUser(service, config_truth_value(enable_root_user));
                        }

                        if (connection_timeout)
                        {
                            serviceSetTimeout(service, atoi(connection_timeout));
                        }

                        if (strlen(max_connections))
                        {
                            serviceSetConnectionLimits(service,
                                                       atoi(max_connections),
                                                       atoi(max_queued_connections),
                                                       atoi(queued_connection_timeout));
                        }

                        if (auth_all_servers)
                        {
                            serviceAuthAllServers(service, config_truth_value(auth_all_servers));
                        }

                        if (strip_db_esc)
                        {
                            serviceStripDbEsc(service, config_truth_value(strip_db_esc));
                        }

                        if (allow_localhost_match_wildcard_host)
                        {
                            serviceEnableLocalhostMatchWildcardHost(
                                service,
                                config_truth_value(allow_localhost_match_wildcard_host));
                        }

                    }

                    obj->element = service;
                }
            }
            else
            {
                obj->element = NULL;
                MXS_ERROR("No router defined for service '%s'.", obj->object);
            }
        }
        else if (!strcmp(type, "server"))
        {
            char *address = config_get_value(obj->parameters, CN_ADDRESS);
            char *port = config_get_value(obj->parameters, CN_PORT);

            if (address && port &&
                (server = server_find(address, atoi(port))) != NULL)
            {
                char *monuser = config_get_value(obj->parameters, CN_MONITORUSER);
                char *monpw = config_get_value(obj->parameters, CN_MONITORPW);
                server_update_credentials(server, monuser, monpw);
                obj->element = server;
            }
            else
            {
                create_new_server(obj);
            }
        }
        obj = obj->next;
    }

    return true;
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
                                        MXS_CONFIG_PARAMETER *params)
{
    bool rval = false;

    if (mod_params)
    {
        for (int i = 0; mod_params[i].name; i++)
        {
            if ((mod_params[i].options & MXS_MODULE_OPT_REQUIRED) &&
                config_get_param(params, mod_params[i].name) == NULL)
            {
                MXS_ERROR("Mandatory parameter '%s' is not defined.", mod_params[i].name);
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
    CONFIG_CONTEXT *obj = context;

    while (obj)
    {
        const char **param_set = NULL;
        const char *module = NULL;
        const char *type;
        const char *module_type = NULL;

        if (obj->parameters && (type = config_get_value(obj->parameters, CN_TYPE)))
        {
            if (!strcmp(type, CN_SERVICE))
            {
                param_set = config_service_params;
                module = config_get_value(obj->parameters, CN_ROUTER);
                module_type = MODULE_ROUTER;
            }
            else if (!strcmp(type, CN_LISTENER))
            {
                param_set = config_listener_params;
            }
            else if (!strcmp(type, CN_MONITOR))
            {
                param_set = config_monitor_params;
                module = config_get_value(obj->parameters, CN_MODULE);
                module_type = MODULE_MONITOR;
            }
            else if (!strcmp(type, CN_FILTER))
            {
                param_set = config_filter_params;
                module = config_get_value(obj->parameters, CN_MODULE);
                module_type = MODULE_FILTER;
            }
        }

        const MXS_MODULE *mod = module ? get_module(module, module_type) : NULL;

        if (param_set != NULL)
        {
            MXS_CONFIG_PARAMETER *params = obj->parameters;
            while (params)
            {
                int found = 0;
                for (int i = 0; param_set[i]; i++)
                {
                    if (!strcmp(params->name, param_set[i]))
                    {
                        found = 1;
                    }
                }

                if (found == 0)
                {
                    if (mod == NULL ||
                        !config_param_is_valid(mod->parameters, params->name, params->value, context))
                    {
                        MXS_ERROR("Unexpected parameter '%s' for object '%s' of type '%s', "
                                  "or '%s' is an invalid value for parameter '%s'.",
                                  params->name, obj->object, type, params->value, params->name);
                        rval = false;
                    }
                    else if (is_path_parameter(mod->parameters, params->name))
                    {
                        process_path_parameter(params);
                    }
                    else
                    {
                        /** Fix old-style object names */
                        config_fix_param(mod->parameters, params);
                    }
                }
                params = params->next;
            }
        }

        if (mod && missing_required_parameters(mod->parameters, obj->parameters))
        {
            rval = false;
        }

        obj = obj->next;
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
                    strncpy(release, new_to, RELEASE_STR_LENGTH);
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

unsigned long config_get_gateway_id()
{
    return gateway.id;
}

bool config_add_param(CONFIG_CONTEXT* obj, const char* key, const char* value)
{
    ss_dassert(config_get_param(obj->parameters, key) == NULL);
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
    ss_dassert(param);
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
    MXS_CONFIG_PARAMETER *param = config_get_param(obj->parameters, key);
    ss_dassert(param);
    char *new_value = MXS_STRDUP(value);
    bool rval;

    if (new_value)
    {
        MXS_FREE(param->value);
        param->value = new_value;
        rval = true;
    }

    return rval;
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

                    if (hashtable_add(context->hash, section, (char*)"") == 0)
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

    if (feof(file))
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

        if ((destptr[offset] = fgetc(file)) == '\n' || feof(file))
        {
            destptr[offset] = '\0';
            break;
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

static json_t* param_value_json(const MXS_CONFIG_PARAMETER* param,
                                const MXS_MODULE* mod)
{
    json_t* rval = NULL;

    for (int i = 0; mod->parameters[i].name; i++)
    {
        if (strcmp(mod->parameters[i].name, param->name) == 0)
        {
            switch (mod->parameters[i].type)
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
        }
    }

    return rval;
}

void config_add_module_params_json(const MXS_MODULE* mod, MXS_CONFIG_PARAMETER* parameters,
                                   const char** type_params, json_t* output)
{
    set<string> param_set;

    for (int i = 0; type_params[i]; i++)
    {
        param_set.insert(type_params[i]);
    }

    for (MXS_CONFIG_PARAMETER* p = parameters; p; p = p->next)
    {
        if (param_set.find(p->name) == param_set.end())
        {
            json_object_set_new(output, p->name, param_value_json(p, mod));
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
    char *router = config_get_value(obj->parameters, CN_ROUTER);
    if (router == NULL)
    {
        obj->element = NULL;
        MXS_ERROR("No router defined for service '%s'.", obj->object);
        return 1;
    }
    else if ((obj->element = service_alloc(obj->object, router)) == NULL)
    {
        MXS_ERROR("Service creation failed.");
        return 1;
    }

    SERVICE* service = (SERVICE*) obj->element;
    int error_count = 0;
    MXS_CONFIG_PARAMETER* param;

    char *retry = config_get_value(obj->parameters, CN_RETRY_ON_FAILURE);
    if (retry)
    {
        serviceSetRetryOnFailure(service, retry);
    }

    char *enable_root_user = config_get_value(obj->parameters, CN_ENABLE_ROOT_USER);
    if (enable_root_user)
    {
        serviceEnableRootUser(service, config_truth_value(enable_root_user));
    }

    char *max_retry_interval = config_get_value(obj->parameters, CN_MAX_RETRY_INTERVAL);

    if (max_retry_interval)
    {
        char *endptr;
        long val = strtol(max_retry_interval, &endptr, 10);

        if (val && *endptr == '\0')
        {
            service_set_retry_interval(service, val);
        }
        else
        {
            MXS_ERROR("Invalid value for 'max_retry_interval': %s", max_retry_interval);
            error_count++;
        }
    }

    char *connection_timeout = config_get_value(obj->parameters, CN_CONNECTION_TIMEOUT);
    if (connection_timeout)
    {
        serviceSetTimeout(service, atoi(connection_timeout));
    }

    const char *max_connections = config_get_value_string(obj->parameters, CN_MAX_CONNECTIONS);
    const char *max_queued_connections = config_get_value_string(obj->parameters, "max_queued_connections");
    const char *queued_connection_timeout = config_get_value_string(obj->parameters, "queued_connection_timeout");
    if (strlen(max_connections))
    {
        serviceSetConnectionLimits(service, atoi(max_connections),
                                   atoi(max_queued_connections), atoi(queued_connection_timeout));
    }

    char *auth_all_servers = config_get_value(obj->parameters, CN_AUTH_ALL_SERVERS);
    if (auth_all_servers)
    {
        serviceAuthAllServers(service, config_truth_value(auth_all_servers));
    }

    char *strip_db_esc = config_get_value(obj->parameters, CN_STRIP_DB_ESC);
    if (strip_db_esc)
    {
        serviceStripDbEsc(service, config_truth_value(strip_db_esc));
    }

    char *weightby = config_get_value(obj->parameters, CN_WEIGHTBY);
    if (weightby)
    {
        serviceWeightBy(service, weightby);
    }

    char *wildcard = config_get_value(obj->parameters, CN_LOCALHOST_MATCH_WILDCARD_HOST);
    if (wildcard)
    {
        serviceEnableLocalhostMatchWildcardHost(service, config_truth_value(wildcard));
    }

    char *user = config_get_value(obj->parameters, CN_USER);
    char *auth = config_get_password(obj->parameters);

    if (user && auth)
    {
        serviceSetUser(service, user, auth);
    }
    else if (!rcap_type_required(service_get_capabilities(service), RCAP_TYPE_NO_AUTH))
    {
        error_count++;
        MXS_ERROR("Service '%s' is missing %s%s%s.",
                  obj->object,
                  user ? "" : "the 'user' parameter",
                  !user && !auth ? " and " : "",
                  auth ? "" : "the 'password' or 'passwd' parameter");
    }

    char *log_auth_warnings = config_get_value(obj->parameters, CN_LOG_AUTH_WARNINGS);
    if (log_auth_warnings)
    {
        int truthval = config_truth_value(log_auth_warnings);
        if (truthval != -1)
        {
            service->log_auth_warnings = (bool) truthval;
        }
        else
        {
            MXS_ERROR("Invalid value for 'log_auth_warnings': %s", log_auth_warnings);
        }
    }

    char *version_string = config_get_value(obj->parameters, CN_VERSION_STRING);
    if (version_string)
    {
        /** Add the 5.5.5- string to the start of the version string if
         * the version string starts with "10.".
         * This mimics MariaDB 10.0 replication which adds 5.5.5- for backwards compatibility. */
        if (version_string[0] != '5')
        {
            size_t len = strlen(version_string) + strlen("5.5.5-") + 1;
            char ver[len];
            snprintf(ver, sizeof(ver), "5.5.5-%s", version_string);
            serviceSetVersionString(service, ver);
        }
        else
        {
            serviceSetVersionString(service, version_string);
        }
    }
    else if (gateway.version_string)
    {
        serviceSetVersionString(service, gateway.version_string);
    }


    /** Store the configuration parameters for the service */
    const MXS_MODULE *mod = get_module(router, MODULE_ROUTER);

    if (mod)
    {
        config_add_defaults(obj, mod->parameters);
        service_add_parameters(service, obj->parameters);
    }
    else
    {
        error_count++;
    }

    return error_count;
}

/**
 * Check if a parameter is a default server parameter.
 * @param param Parameter name
 * @return True if it is one of the standard server parameters
 */
bool is_normal_server_parameter(const char *param)
{
    for (int i = 0; server_params[i]; i++)
    {
        if (strcmp(param, server_params[i]) == 0)
        {
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
    int error_count = 0;
    char *address = config_get_value(obj->parameters, CN_ADDRESS);
    char *port = config_get_value(obj->parameters, CN_PORT);
    char *protocol = config_get_value(obj->parameters, CN_PROTOCOL);
    char *monuser = config_get_value(obj->parameters, CN_MONITORUSER);
    char *monpw = config_get_value(obj->parameters, CN_MONITORPW);
    char *auth = config_get_value(obj->parameters, CN_AUTHENTICATOR);
    char *auth_opts = config_get_value(obj->parameters, CN_AUTHENTICATOR_OPTIONS);

    if (address && port && protocol)
    {
        if ((obj->element = server_alloc(obj->object, address, atoi(port), protocol, auth, auth_opts)) == NULL)
        {
            MXS_ERROR("Failed to create a new server, memory allocation failed.");
            error_count++;
        }
    }
    else
    {
        obj->element = NULL;
        MXS_ERROR("Server '%s' is missing a required configuration parameter. A "
                  "server must have address, port and protocol defined.", obj->object);
        error_count++;
    }

    if (error_count == 0)
    {
        SERVER *server = (SERVER*)obj->element;

        if (monuser && monpw)
        {
            server_add_mon_user(server, monuser, monpw);
        }
        else if (monuser && monpw == NULL)
        {
            MXS_ERROR("Server '%s' has a monitoruser defined but no corresponding "
                      "password.", obj->object);
            error_count++;
        }

        char *endptr;
        const char *poolmax = config_get_value_string(obj->parameters, CN_PERSISTPOOLMAX);
        if (poolmax)
        {
            long int persistpoolmax = strtol(poolmax, &endptr, 0);
            if (*endptr != '\0' || persistpoolmax < 0)
            {
                MXS_ERROR("Invalid value for 'persistpoolmax' for server %s: %s",
                          server->unique_name, poolmax);
                error_count++;
            }
            else
            {
                server->persistpoolmax = persistpoolmax;
            }
        }

        const char *persistmax = config_get_value_string(obj->parameters, CN_PERSISTMAXTIME);
        if (persistmax)
        {
            long int persistmaxtime = strtol(persistmax, &endptr, 0);
            if (*endptr != '\0' || persistmaxtime < 0)
            {
                MXS_ERROR("Invalid value for 'persistmaxtime' for server %s: %s",
                          server->unique_name, persistmax);
                error_count++;
            }
            else
            {
                server->persistmaxtime = persistmaxtime;
            }
        }

        const char* proxy_protocol = config_get_value_string(obj->parameters, CN_PROXY_PROTOCOL);
        if (*proxy_protocol)
        {
            int truth_value = config_truth_value(proxy_protocol);
            if (truth_value == 1)
            {
                server->proxy_protocol = true;
            }
            else if (truth_value == 0)
            {
                server->proxy_protocol = false;
            }
            else
            {
                MXS_ERROR("Invalid value for '%s' for server %s: %s",
                          CN_PROXY_PROTOCOL, server->unique_name, proxy_protocol);
                error_count++;
            }
        }

        MXS_CONFIG_PARAMETER *params = obj->parameters;

        server->server_ssl = make_ssl_structure(obj, false, &error_count);
        if (server->server_ssl && listener_init_SSL(server->server_ssl) != 0)
        {
            MXS_ERROR("Unable to initialize server SSL");
        }

        while (params)
        {
            if (!is_normal_server_parameter(params->name))
            {
                server_add_parameter(server, params->name, params->value);
            }
            params = params->next;
        }
    }
    return error_count;
}

/**
 * Configure a new service
 *
 * Add servers, router options and filters to a new service.
 * @param context The complete configuration context
 * @param obj The service configuration context
 * @return Number of errors
 */
int configure_new_service(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj)
{
    int error_count = 0;
    char *filters = config_get_value(obj->parameters, CN_FILTERS);
    char *servers = config_get_value(obj->parameters, CN_SERVERS);
    char *monitor = config_get_value(obj->parameters, CN_MONITOR);
    char *roptions = config_get_value(obj->parameters, CN_ROUTER_OPTIONS);
    SERVICE *service = (SERVICE*)obj->element;

    if (service)
    {
        if (monitor)
        {
            if (servers)
            {
                MXS_WARNING("Both `monitor` and `servers` are defined. Only the "
                            "value of `monitor` will be used.");
            }

            /** `monitor` takes priority over `servers` */
            servers = NULL;

            for (CONFIG_CONTEXT *ctx = context; ctx; ctx = ctx->next)
            {
                if (strcmp(ctx->object, monitor) == 0)
                {
                    servers = config_get_value(ctx->parameters, CN_SERVERS);
                    break;
                }
            }

            if (servers == NULL)
            {
                MXS_ERROR("Unable to find monitor '%s'.", monitor);
                error_count++;
            }
        }

        if (servers)
        {
            char srv_list[strlen(servers) + 1];
            strcpy(srv_list, servers);
            char *lasts;
            char *s = strtok_r(srv_list, ",", &lasts);
            while (s)
            {
                CONFIG_CONTEXT *obj1 = context;
                int found = 0;
                while (obj1)
                {
                    if (strcmp(trim(s), obj1->object) == 0 && obj1->element)
                    {
                        found = 1;
                        serviceAddBackend(service, (SERVER*)obj1->element);
                        break;
                    }
                    obj1 = obj1->next;
                }

                if (!found)
                {
                    MXS_ERROR("Unable to find server '%s' that is "
                              "configured as part of service '%s'.", s, obj->object);
                    error_count++;
                }
                s = strtok_r(NULL, ",", &lasts);
            }
        }

        if (roptions)
        {
            char *lasts;
            char *s = strtok_r(roptions, ",", &lasts);
            while (s)
            {
                serviceAddRouterOption(service, s);
                s = strtok_r(NULL, ",", &lasts);
            }
        }

        if (filters)
        {
            if (!serviceSetFilters(service, filters))
            {
                error_count++;
            }
        }
    }

    return error_count;
}

/**
 * Create a new monitor
 * @param context The complete configuration context
 * @param obj Monitor configuration context
 * @param monitorhash Hashtable containing the servers that are already monitored
 * @return Number of errors
 */
int create_new_monitor(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj, HASHTABLE* monitorhash)
{
    int error_count = 0;

    char *module = config_get_value(obj->parameters, CN_MODULE);
    if (module)
    {
        if ((obj->element = monitor_alloc(obj->object, module)) == NULL)
        {
            MXS_ERROR("Failed to create monitor '%s'.", obj->object);
            error_count++;
        }
    }
    else
    {
        obj->element = NULL;
        MXS_ERROR("Monitor '%s' is missing the required 'module' parameter.", obj->object);
        error_count++;
    }

    char *servers = config_get_value(obj->parameters, CN_SERVERS);

    if (error_count == 0)
    {
        MXS_MONITOR* monitor = (MXS_MONITOR*)obj->element;
        const MXS_MODULE *mod = get_module(module, MODULE_MONITOR);

        if (mod)
        {
            config_add_defaults(obj, mod->parameters);
            monitorAddParameters(monitor, obj->parameters);
        }
        else
        {
            error_count++;
        }

        char *interval_str = config_get_value(obj->parameters, CN_MONITOR_INTERVAL);
        if (interval_str)
        {
            char *endptr;
            long interval = strtol(interval_str, &endptr, 0);
            /* The interval must be >0 because it is used as a divisor.
                Perhaps a greater minimum value should be added? */
            if (*endptr == '\0' && interval > 0)
            {
                monitorSetInterval(monitor, (unsigned long)interval);
            }
            else
            {
                MXS_NOTICE("Invalid '%s' parameter for monitor '%s', "
                           "using default value of %d milliseconds.",
                           CN_MONITOR_INTERVAL, obj->object, DEFAULT_MONITOR_INTERVAL);
            }
        }
        else
        {
            MXS_NOTICE("Monitor '%s' is missing the '%s' parameter, "
                       "using default value of %d milliseconds.",
                       CN_MONITOR_INTERVAL, obj->object, DEFAULT_MONITOR_INTERVAL);
        }

        char *journal_age = config_get_value(obj->parameters, CN_JOURNAL_MAX_AGE);
        if (journal_age)
        {
            char *endptr;
            long interval = strtol(journal_age, &endptr, 0);

            if (*endptr == '\0' && interval > 0)
            {
                monitorSetJournalMaxAge(monitor, (time_t)interval);
            }
            else
            {
                error_count++;
                MXS_NOTICE("Invalid '%s' parameter for monitor '%s'",
                           CN_JOURNAL_MAX_AGE, obj->object);
            }
        }
        else
        {
            MXS_NOTICE("Monitor '%s' is missing the '%s' parameter, "
                       "using default value of %d seconds.",
                       obj->object, CN_JOURNAL_MAX_AGE, DEFAULT_JOURNAL_MAX_AGE);
        }

        char *script_timeout = config_get_value(obj->parameters, CN_SCRIPT_TIMEOUT);
        if (script_timeout)
        {
            char *endptr;
            long interval = strtol(script_timeout, &endptr, 0);

            if (*endptr == '\0' && interval > 0)
            {
                monitorSetScriptTimeout(monitor, (uint32_t)interval);
            }
            else
            {
                error_count++;
                MXS_NOTICE("Invalid '%s' parameter for monitor '%s'",
                           CN_SCRIPT_TIMEOUT, obj->object);
            }
        }
        else
        {
            MXS_NOTICE("Monitor '%s' is missing the '%s' parameter, "
                       "using default value of %d seconds.",
                       obj->object, CN_SCRIPT_TIMEOUT, DEFAULT_SCRIPT_TIMEOUT);
        }

        char *failover = config_get_value(obj->parameters, CN_FAILOVER);
        if (failover)
        {
            int val = config_truth_value(failover);

            if (val != -1)
            {
                monitorSetFailover(monitor, val);
            }
            else
            {
                error_count++;
                MXS_NOTICE("Invalid '%s' parameter for monitor '%s'",
                           CN_FAILOVER, obj->object);
            }
        }

        char *failover_timeout = config_get_value(obj->parameters, CN_FAILOVER_TIMEOUT);
        if (failover_timeout)
        {
            char *endptr;
            long interval = strtol(failover_timeout, &endptr, 0);

            if (*endptr == '\0' && interval > 0)
            {
                monitorSetFailoverTimeout(monitor, (uint32_t)interval);
            }
            else
            {
                error_count++;
                MXS_NOTICE("Invalid '%s' parameter for monitor '%s'",
                           CN_FAILOVER_TIMEOUT, obj->object);
            }
        }

        char *connect_timeout = config_get_value(obj->parameters, CN_BACKEND_CONNECT_TIMEOUT);
        if (connect_timeout)
        {
            if (!monitorSetNetworkTimeout(monitor, MONITOR_CONNECT_TIMEOUT, atoi(connect_timeout)))
            {
                MXS_ERROR("Failed to set '%s'", CN_BACKEND_CONNECT_TIMEOUT);
                error_count++;
            }
        }

        char *read_timeout = config_get_value(obj->parameters, CN_BACKEND_READ_TIMEOUT);
        if (read_timeout)
        {
            if (!monitorSetNetworkTimeout(monitor, MONITOR_READ_TIMEOUT, atoi(read_timeout)))
            {
                MXS_ERROR("Failed to set '%s'", CN_BACKEND_READ_TIMEOUT);
                error_count++;
            }
        }

        char *write_timeout = config_get_value(obj->parameters, CN_BACKEND_WRITE_TIMEOUT);
        if (write_timeout)
        {
            if (!monitorSetNetworkTimeout(monitor, MONITOR_WRITE_TIMEOUT, atoi(write_timeout)))
            {
                MXS_ERROR("Failed to set '%s'", CN_BACKEND_WRITE_TIMEOUT);
                error_count++;
            }
        }

        char *connect_attempts = config_get_value(obj->parameters, CN_BACKEND_CONNECT_ATTEMPTS);
        if (connect_attempts)
        {
            if (!monitorSetNetworkTimeout(monitor, MONITOR_CONNECT_ATTEMPTS, atoi(connect_attempts)))
            {
                MXS_ERROR("Failed to set '%s'", CN_BACKEND_CONNECT_ATTEMPTS);
                error_count++;
            }
        }

        if (servers)
        {
            /* get the servers to monitor */
            char *s, *lasts;
            s = strtok_r(servers, ",", &lasts);
            while (s)
            {
                CONFIG_CONTEXT *obj1 = context;
                int found = 0;
                while (obj1)
                {
                    if (strcmp(trim(s), obj1->object) == 0 && obj->element && obj1->element)
                    {
                        found = 1;
                        if (hashtable_add(monitorhash, obj1->object, (char*)"") == 0)
                        {
                            MXS_WARNING("Multiple monitors are monitoring server [%s]. "
                                        "This will cause undefined behavior.",
                                        obj1->object);
                        }
                        monitorAddServer(monitor, (SERVER*)obj1->element);
                    }
                    obj1 = obj1->next;
                }
                if (!found)
                {
                    MXS_ERROR("Unable to find server '%s' that is "
                              "configured in the monitor '%s'.", s, obj->object);
                    error_count++;
                }

                s = strtok_r(NULL, ",", &lasts);
            }
        }

        char *user = config_get_value(obj->parameters, CN_USER);
        char *passwd = config_get_password(obj->parameters);
        if (user && passwd)
        {
            monitorAddUser(monitor, user, passwd);
        }
        else if (user)
        {
            MXS_ERROR("Monitor '%s' defines a username but does not define a password.",
                      obj->object);
            error_count++;
        }
    }

    return error_count;
}

/**
 * Create a new listener for a service
 * @param obj Listener configuration context
 * @param startnow If true, start the listener now
 * @return Number of errors
 */
int create_new_listener(CONFIG_CONTEXT *obj)
{
    int error_count = 0;
    char *raw_service_name = config_get_value(obj->parameters, CN_SERVICE);
    char *port = config_get_value(obj->parameters, CN_PORT);
    char *address = config_get_value(obj->parameters, CN_ADDRESS);
    char *protocol = config_get_value(obj->parameters, CN_PROTOCOL);
    char *socket = config_get_value(obj->parameters, CN_SOCKET);
    char *authenticator = config_get_value(obj->parameters, CN_AUTHENTICATOR);
    char *authenticator_options = config_get_value(obj->parameters, CN_AUTHENTICATOR_OPTIONS);

    if (raw_service_name && protocol && (socket || port))
    {
        char service_name[strlen(raw_service_name) + 1];
        strcpy(service_name, raw_service_name);
        fix_section_name(service_name);

        SERVICE *service = service_find(service_name);
        if (service)
        {
            SSL_LISTENER *ssl_info = make_ssl_structure(obj, true, &error_count);
            if (socket)
            {
                if (serviceHasListener(service, obj->object, protocol, address, 0))
                {
                    MXS_ERROR("Listener '%s' for service '%s' already has a socket at '%s.",
                              obj->object, service_name, socket);
                    error_count++;
                }
                else
                {
                    serviceCreateListener(service, obj->object, protocol, socket, 0,
                                          authenticator, authenticator_options, ssl_info);
                }
            }

            if (port)
            {
                if (serviceHasListener(service, obj->object, protocol, address, atoi(port)))
                {
                    MXS_ERROR("Listener '%s', for service '%s', already have port %s.",
                              obj->object,
                              service_name,
                              port);
                    error_count++;
                }
                else
                {
                    serviceCreateListener(service, obj->object, protocol, address, atoi(port),
                                          authenticator, authenticator_options, ssl_info);
                }
            }

            if (ssl_info && error_count)
            {
                free_ssl_structure(ssl_info);
            }
        }
        else
        {
            MXS_ERROR("Listener '%s', service '%s' not found.", obj->object,
                      service_name);
            error_count++;
        }
    }
    else
    {
        MXS_ERROR("Listener '%s' is missing a required parameter. A Listener "
                  "must have a service, port and protocol defined.", obj->object);
        error_count++;
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
    char *module = config_get_value(obj->parameters, CN_MODULE);

    if (module)
    {
        if ((obj->element = filter_alloc(obj->object, module)))
        {
            MXS_FILTER_DEF* filter_def = (MXS_FILTER_DEF*)obj->element;
            char *options = config_get_value(obj->parameters, CN_OPTIONS);
            if (options)
            {
                char *lasts;
                char *s = strtok_r(options, ",", &lasts);
                while (s)
                {
                    filter_add_option(filter_def, s);
                    s = strtok_r(NULL, ",", &lasts);
                }
            }

            const MXS_MODULE *mod = get_module(module, MODULE_FILTER);

            if (mod)
            {
                config_add_defaults(obj, mod->parameters);
            }
            else
            {
                error_count++;
            }

            for (MXS_CONFIG_PARAMETER *p = obj->parameters; p; p = p->next)
            {
                filter_add_parameter(filter_def, p->name, p->value);
            }
        }
        else
        {
            MXS_ERROR("Failed to create filter '%s'. Memory allocation failed.",
                      obj->object);
            error_count++;
        }
    }
    else
    {
        MXS_ERROR("Filter '%s' has no module defined to load.", obj->object);
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
            mask |= S_IRUSR;
            mode |= R_OK;
        }
        if (params->options & MXS_MODULE_OPT_PATH_X_OK)
        {
            mask |= S_IXUSR;
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
        fix_section_name(start);
        dest += sep;
        dest += start;
        sep = ",";
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
                fix_section_name(p->value);
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
    fix_section_name(fixed_value);

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
                ss_dassert(false);
                break;
            }
        }
    }

    return valid;
}

int config_parse_server_list(const char *servers, char ***output_array)
{
    ss_dassert(servers);

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
        fix_section_name(srv_name_tmp);

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
    json_object_set_new(param, CN_PASSIVE, json_boolean(cnf->passive));

    json_object_set_new(param, CN_QUERY_CLASSIFIER, json_string(cnf->qc_name));

    if (cnf->qc_args)
    {
        json_object_set_new(param, CN_QUERY_CLASSIFIER_ARGS, json_string(cnf->qc_args));
    }

    json_t* attr = json_object();
    time_t started = maxscale_started();
    time_t activated = started + HB_TO_SEC(cnf->promoted_at);
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
        ss_dassert(dot);
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

/**
 * Converts a string into the corresponding value, interpreting
 * IEC or SI prefixes used as suffixes appropriately.
 *
 * @param value A numerical string, possibly suffixed by a IEC
 *              binary prefix or SI prefix.
 *
 * @return The corresponding size.
 */
static uint64_t get_suffixed_size(const char* value)
{
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

    return size;
}
