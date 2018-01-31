/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file config.c  - Read the gateway.cnf configuration file
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 21/06/13     Mark Riddoch            Initial implementation
 * 08/07/13     Mark Riddoch            Addition on monitor module support
 * 23/07/13     Mark Riddoch            Addition on default monitor password
 * 06/02/14     Massimiliano Pinto      Added support for enable/disable root user in services
 * 14/02/14     Massimiliano Pinto      Added enable_root_user in the service_params list
 * 11/03/14     Massimiliano Pinto      Added Unix socket support
 * 11/05/14     Massimiliano Pinto      Added version_string support to service
 * 19/05/14     Mark Riddoch            Added unique names from section headers
 * 29/05/14     Mark Riddoch            Addition of filter definition
 * 23/05/14     Massimiliano Pinto      Added automatic set of maxscale-id: first listening ipv4_raw + port + pid
 * 28/05/14     Massimiliano Pinto      Added detect_replication_lag parameter
 * 28/08/14     Massimiliano Pinto      Added detect_stale_master parameter
 * 09/09/14     Massimiliano Pinto      Added localhost_match_wildcard_host parameter
 * 12/09/14     Mark Riddoch            Addition of checks on servers list and
 *                                      internal router suppression of messages
 * 30/10/14     Massimiliano Pinto      Added disable_master_failback parameter
 * 07/11/14     Massimiliano Pinto      Addition of monitor timeouts for connect/read/write
 * 20/02/15     Markus Mäkelä           Added connection_timeout parameter for services
 * 05/03/15     Massimiliano Pinto      Added notification_feedback support
 * 20/04/15     Guillaume Lefranc       Added available_when_donor parameter
 * 22/04/15     Martin Brampton         Added disable_master_role_setting parameter
 * 26/01/16     Martin Brampton         Transfer SSL processing to listener
 * 31/05/16     Martin Brampton         Implement connection throttling, initially no queue
 *
 * @endverbatim
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

#include <maxscale/alloc.h>
#include <maxscale/housekeeper.h>
#include <maxscale/limits.h>
#include <maxscale/log_manager.h>
#include <maxscale/notification.h>
#include <maxscale/pcre2.h>
#include <maxscale/spinlock.h>
#include <maxscale/utils.h>
#include <maxscale/paths.h>

#include "maxscale/config.h"
#include "maxscale/filter.h"
#include "maxscale/service.h"
#include "maxscale/monitor.h"
#include "maxscale/modules.h"

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
static int handle_feedback_item(const char *, const char *);
static void global_defaults();
static void feedback_defaults();
static bool check_config_objects(CONFIG_CONTEXT *context);
static int maxscale_getline(char** dest, int* size, FILE* file);

int config_get_ifaddr(unsigned char *output);
static int config_get_release_string(char* release);
FEEDBACK_CONF *config_get_feedback_data();
bool config_has_duplicate_sections(const char* config, DUPLICATE_CONTEXT* context);
int create_new_service(CONFIG_CONTEXT *obj);
int create_new_server(CONFIG_CONTEXT *obj);
int create_new_monitor(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj, HASHTABLE* monitorhash);
int create_new_listener(CONFIG_CONTEXT *obj);
int create_new_filter(CONFIG_CONTEXT *obj);
int configure_new_service(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj);

static const char *config_file = NULL;
static MXS_CONFIG gateway;
static FEEDBACK_CONF feedback;
char *version_string = NULL;
static bool is_persisted_config = false; /**< True if a persisted configuration file is being parsed */

static const char *service_params[] =
{
    "type",
    "router",
    "router_options",
    "servers",
    "monitor",
    "user",
    "passwd", // DEPRECATE: See config_get_password.
    "password",
    "enable_root_user",
    "max_connections",
    "max_queued_connections",
    "queued_connection_timeout",
    "connection_timeout",
    "auth_all_servers",
    "strip_db_esc",
    "localhost_match_wildcard_host",
    "version_string",
    "filters",
    "weightby",
    "log_auth_warnings",
    "retry_on_failure",
    NULL
};

static const char *listener_params[] =
{
    "authenticator_options",
    "type",
    "service",
    "protocol",
    "port",
    "address",
    "socket",
    "authenticator",
    "ssl_cert",
    "ssl_ca_cert",
    "ssl",
    "ssl_key",
    "ssl_version",
    "ssl_cert_verify_depth",
    "ssl_verify_peer_certificate",
    NULL
};

static const char *monitor_params[] =
{
    "type",
    "module",
    "servers",
    "user",
    "passwd",   // DEPRECATE: See config_get_password.
    "password",
    "script",
    "events",
    "monitor_interval",
    "backend_connect_timeout",
    "backend_read_timeout",
    "backend_write_timeout",
    NULL
};

static const char *filter_params[] =
{
    "type",
    "module",
    NULL
};

static const char *server_params[] =
{
    "type",
    "protocol",
    "port",
    "address",
    "authenticator",
    "authenticator_options",
    "monitoruser",
    "monitorpw",
    "persistpoolmax",
    "persistmaxtime",
    "ssl_cert",
    "ssl_ca_cert",
    "ssl",
    "ssl_key",
    "ssl_version",
    "ssl_cert_verify_depth",
    "ssl_verify_peer_certificate",
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
    char *dest = MXS_MALLOC(destsize);

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
            char* tmp = MXS_REALLOC(dest, destsize_tmp);
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

    if (strcmp(section, "gateway") == 0 || strcasecmp(section, "MaxScale") == 0)
    {
        return handle_global_item(name, value);
    }
    else if (strcasecmp(section, "feedback") == 0)
    {
        return handle_feedback_item(name, value);
    }
    else if (strlen(section) == 0)
    {
        MXS_ERROR("Parameter '%s=%s' declared outside a section.", name, value);
        return 0;
    }

    /*
     * If we already have some parameters for the object
     * add the parameters to that object. If not create
     * a new object.
     */
    while (ptr && strcmp(ptr->object, section) != 0)
    {
        ptr = ptr->next;
    }

    if (!ptr)
    {
        if ((ptr = config_context_create(section)) == NULL)
        {
            return 0;
        }

        ptr->next = cntxt->next;
        cntxt->next = ptr;
    }

    if (config_get_param(ptr->parameters, name))
    {
        if (!config_append_param(ptr, name, value))
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
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_WARNING("Could not access %s, not reading: %s",
                        dir, strerror_r(errno, errbuf, sizeof(errbuf)));
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
        CONFIG_CONTEXT ccontext = {.object = ""};

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

    global_defaults();
    feedback_defaults();

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

        global_defaults();
        feedback_defaults();

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
        char *type = config_get_value(obj->parameters, "type");
        if (type)
        {
            if (!strcmp(type, "service"))
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
            char *type = config_get_value(obj->parameters, "type");
            if (type)
            {
                if (!strcmp(type, "service"))
                {
                    error_count += configure_new_service(context, obj);
                }
                else if (!strcmp(type, "listener"))
                {
                    error_count += create_new_listener(obj);
                }
                else if (!strcmp(type, "monitor"))
                {
                    error_count += create_new_monitor(context, obj, monitorhash);
                }
                else if (strcmp(type, "server") != 0 && strcmp(type, "filter") != 0)
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
    char *password = config_get_value(params, "password");
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

MXS_CONFIG_PARAMETER* config_clone_param(const MXS_CONFIG_PARAMETER* param)
{
    MXS_CONFIG_PARAMETER *p2 = MXS_MALLOC(sizeof(MXS_CONFIG_PARAMETER));

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

/**
 * Return the feedback config data pointer
 *
 * @return  The feedback config data pointer
 */
FEEDBACK_CONF *
config_get_feedback_data()
{
    return &feedback;
}

static struct
{
    char* name;
    int   priority;
    char* replacement;
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
    if (strcmp(name, "threads") == 0)
    {
        if (strcmp(value, "auto") == 0)
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
    else if (strcmp(name, "non_blocking_polls") == 0)
    {
        gateway.n_nbpoll = atoi(value);
    }
    else if (strcmp(name, "poll_sleep") == 0)
    {
        gateway.pollsleep = atoi(value);
    }
    else if (strcmp(name, "ms_timestamp") == 0)
    {
        mxs_log_set_highprecision_enabled(config_truth_value((char*)value));
    }
    else if (strcmp(name, "skip_permission_checks") == 0)
    {
        gateway.skip_permission_checks = config_truth_value((char*)value);
    }
    else if (strcmp(name, "auth_connect_timeout") == 0)
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
    else if (strcmp(name, "auth_read_timeout") == 0)
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
    else if (strcmp(name, "auth_write_timeout") == 0)
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
    else if (strcmp(name, "query_classifier") == 0)
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
    else if (strcmp(name, "query_classifier_args") == 0)
    {
        gateway.qc_args = MXS_STRDUP_A(value);
    }
    else if (strcmp(name, "query_retries") == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval >= 0)
        {
            gateway.query_retries = intval;
        }
        else
        {
            MXS_ERROR("Invalid timeout value for 'query_retries': %s", value);
            return 0;
        }
    }
    else if (strcmp(name, "query_retry_timeout") == 0)
    {
        char* endptr;
        int intval = strtol(value, &endptr, 0);
        if (*endptr == '\0' && intval > 0)
        {
            gateway.query_retries = intval;
        }
        else
        {
            MXS_ERROR("Invalid timeout value for 'query_retry_timeout': %s", value);
            return 0;
        }
    }
    else if (strcmp(name, "log_throttling") == 0)
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
    else if (strcmp(name, "local_address") == 0)
    {
        gateway.local_address = MXS_STRDUP_A(value);
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

    ssl = config_get_value(obj->parameters, "ssl");

    if (ssl)
    {
        if (!strcmp(ssl, "required"))
        {
            if ((new_ssl = MXS_CALLOC(1, sizeof(SSL_LISTENER))) == NULL)
            {
                return NULL;
            }
            new_ssl->ssl_method_type = SERVICE_SSL_TLS_MAX;
            ssl_cert = config_get_value(obj->parameters, "ssl_cert");
            ssl_key = config_get_value(obj->parameters, "ssl_key");
            ssl_ca_cert = config_get_value(obj->parameters, "ssl_ca_cert");
            ssl_version = config_get_value(obj->parameters, "ssl_version");
            char* ssl_verify_peer_certificate = config_get_value(obj->parameters, "ssl_verify_peer_certificate");
            ssl_cert_verify_depth = config_get_value(obj->parameters, "ssl_cert_verify_depth");
            new_ssl->ssl_init_done = false;
            new_ssl->ssl_cert_verify_depth = 9; // Default of 9 as per Linux man page
            new_ssl->ssl_verify_peer_certificate = true;

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

            if (ssl_verify_peer_certificate)
            {
                int rv = config_truth_value(ssl_verify_peer_certificate);
                if (rv == -1)
                {
                    MXS_ERROR("Invalid parameter value for 'ssl_verify_peer_certificate"
                              " for service '%s': %s", obj->object, ssl_verify_peer_certificate);
                    local_errors++;
                }
                else
                {
                    new_ssl->ssl_verify_peer_certificate = rv;
                }
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

/**
 * Configuration handler for items in the feedback [feedback] section
 *
 * @param name  The item name
 * @param value The item value
 * @return 0 on error
 */
static  int
handle_feedback_item(const char *name, const char *value)
{
    int i;
    if (strcmp(name, "feedback_enable") == 0)
    {
        feedback.feedback_enable = config_truth_value(value);
    }
    else if (strcmp(name, "feedback_user_info") == 0)
    {
        feedback.feedback_user_info = MXS_STRDUP_A(value);
    }
    else if (strcmp(name, "feedback_url") == 0)
    {
        feedback.feedback_url = MXS_STRDUP_A(value);
    }
    if (strcmp(name, "feedback_timeout") == 0)
    {
        feedback.feedback_timeout = atoi(value);
    }
    if (strcmp(name, "feedback_connect_timeout") == 0)
    {
        feedback.feedback_connect_timeout = atoi(value);
    }
    if (strcmp(name, "feedback_frequency") == 0)
    {
        feedback.feedback_frequency = atoi(value);
    }
    return 1;
}

/**
 * Set the defaults for the global configuration options
 */
static void
global_defaults()
{
    uint8_t mac_addr[6] = "";
    struct utsname uname_data;
    gateway.n_threads = DEFAULT_NTHREADS;
    gateway.n_nbpoll = DEFAULT_NBPOLLS;
    gateway.pollsleep = DEFAULT_POLLSLEEP;
    gateway.auth_conn_timeout = DEFAULT_AUTH_CONNECT_TIMEOUT;
    gateway.auth_read_timeout = DEFAULT_AUTH_READ_TIMEOUT;
    gateway.auth_write_timeout = DEFAULT_AUTH_WRITE_TIMEOUT;
    gateway.skip_permission_checks = false;
    gateway.query_retries = DEFAULT_QUERY_RETRIES;
    gateway.query_retry_timeout = DEFAULT_QUERY_RETRY_TIMEOUT;

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
}

/**
 * Set the defaults for the feedback configuration options
 */
static void
feedback_defaults()
{
    feedback.feedback_enable = 0;
    feedback.feedback_user_info = NULL;
    feedback.feedback_last_action = _NOTIFICATION_SEND_PENDING;
    feedback.feedback_timeout = _NOTIFICATION_OPERATION_TIMEOUT;
    feedback.feedback_connect_timeout = _NOTIFICATION_CONNECT_TIMEOUT;
    feedback.feedback_url = NULL;
    feedback.feedback_frequency = 1800;
    feedback.release_info = gateway.release_string;
    feedback.sysname = gateway.sysname;
    feedback.mac_sha1 = gateway.mac_sha1;
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
        char *type = config_get_value(obj->parameters, "type");
        if (type == NULL)
        {
            MXS_ERROR("Configuration object %s has no type.", obj->object);
        }
        else if (!strcmp(type, "service"))
        {
            char *router = config_get_value(obj->parameters, "router");
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

                    enable_root_user = config_get_value(obj->parameters, "enable_root_user");

                    connection_timeout = config_get_value(obj->parameters, "connection_timeout");
                    max_connections = config_get_value_string(obj->parameters, "max_connections");
                    max_queued_connections = config_get_value_string(obj->parameters, "max_queued_connections");
                    queued_connection_timeout = config_get_value_string(obj->parameters, "queued_connection_timeout");
                    user = config_get_value(obj->parameters, "user");
                    auth = config_get_password(obj->parameters);

                    auth_all_servers = config_get_value(obj->parameters, "auth_all_servers");
                    strip_db_esc = config_get_value(obj->parameters, "strip_db_esc");
                    version_string = config_get_value(obj->parameters, "version_string");
                    allow_localhost_match_wildcard_host =
                        config_get_value(obj->parameters, "localhost_match_wildcard_host");

                    char *log_auth_warnings = config_get_value(obj->parameters, "log_auth_warnings");
                    int truthval;
                    if (log_auth_warnings && (truthval = config_truth_value(log_auth_warnings)) != -1)
                    {
                        service->log_auth_warnings = (bool)truthval;
                    }

                    if (version_string)
                    {
                        if (service->version_string)
                        {
                            MXS_FREE(service->version_string);
                        }
                        service->version_string = MXS_STRDUP_A(version_string);
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
            char *address = config_get_value(obj->parameters, "address");
            char *port = config_get_value(obj->parameters, "port");

            if (address && port &&
                (server = server_find(address, atoi(port))) != NULL)
            {
                char *monuser = config_get_value(obj->parameters, "monuser");
                char *monpw = config_get_value(obj->parameters, "monpw");
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
        char *value = MXS_MALLOC(size);
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

        if (obj->parameters && (type = config_get_value(obj->parameters, "type")))
        {
            if (!strcmp(type, "service"))
            {
                param_set = service_params;
                module = config_get_value(obj->parameters, "router");
                module_type = MODULE_ROUTER;
            }
            else if (!strcmp(type, "listener"))
            {
                param_set = listener_params;
            }
            else if (!strcmp(type, "monitor"))
            {
                param_set = monitor_params;
                module = config_get_value(obj->parameters, "module");
                module_type = MODULE_MONITOR;
            }
            else if (!strcmp(type, "filter"))
            {
                param_set = filter_params;
                module = config_get_value(obj->parameters, "module");
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

static char *InternalRouters[] =
{
    "debugcli",
    "cli",
    "maxinfo",
    "binlogrouter",
    "testroute",
    "avrorouter",
    NULL
};

/**
 * Determine if the router is one of the special internal services that
 * MaxScale offers.
 *
 * @param router        The router name
 * @return      Non-zero if the router is in the InternalRouters table
 */
bool is_internal_service(const char *router)
{
    if (router)
    {
        for (int i = 0; InternalRouters[i]; i++)
        {
            if (strcmp(router, InternalRouters[i]) == 0)
            {
                return true;
            }
        }
    }
    return false;
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
    char distribution[_RELEASE_STR_LENGTH] = "";
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
            int k = 0;
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
                new_to = strncpy(distribution, found.gl_pathv[0] + 5, _RELEASE_STR_LENGTH - 1);
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
                    strncpy(release, new_to, _RELEASE_STR_LENGTH);
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

/**
 * Add the 'send_feedback' task to the task list
 */
void
config_enable_feedback_task(void)
{
    FEEDBACK_CONF *cfg = config_get_feedback_data();
    int url_set = 0;
    int user_info_set = 0;
    int enable_set = cfg->feedback_enable;

    url_set = cfg->feedback_url != NULL && strlen(cfg->feedback_url);
    user_info_set = cfg->feedback_user_info != NULL && strlen(cfg->feedback_user_info);

    if (enable_set && url_set && user_info_set)
    {
        /* Add the task to the tasl list */
        if (hktask_add("send_feedback", module_feedback_send, cfg, cfg->feedback_frequency))
        {
            MXS_NOTICE("Notification service feedback task started: URL=%s, User-Info=%s, "
                       "Frequency %u seconds",
                       cfg->feedback_url,
                       cfg->feedback_user_info,
                       cfg->feedback_frequency);
        }
    }
    else
    {
        if (enable_set)
        {
            MXS_ERROR("Notification service feedback cannot start: feedback_enable=1 but"
                      " some required parameters are not set: %s%s%s",
                      url_set == 0 ? "feedback_url is not set" : "",
                      (user_info_set == 0 && url_set == 0) ? ", " : "",
                      user_info_set == 0 ? "feedback_user_info is not set" : "");
        }
        else
        {
            MXS_INFO("Notification service feedback is not enabled.");
        }
    }
}

/**
 * Remove the 'send_feedback' task
 */
void
config_disable_feedback_task(void)
{
    hktask_remove("send_feedback");
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
    char *buffer = MXS_MALLOC(size * sizeof(char));

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

                    if (hashtable_add(context->hash, section, "") == 0)
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
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Failed to open file '%s': %s", filename,
                      strerror_r(errno, errbuf, sizeof(errbuf)));
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
            char* tmp = (char*) MXS_REALLOC(destptr, *size * 2);
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
 * Create a new router for a service
 * @param obj Service configuration context
 * @return True if configuration was successful, false if an error occurred.
 */
int create_new_service(CONFIG_CONTEXT *obj)
{
    char *router = config_get_value(obj->parameters, "router");
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

    char *retry = config_get_value(obj->parameters, "retry_on_failure");
    if (retry)
    {
        serviceSetRetryOnFailure(obj->element, retry);
    }

    char *enable_root_user = config_get_value(obj->parameters, "enable_root_user");
    if (enable_root_user)
    {
        serviceEnableRootUser(obj->element, config_truth_value(enable_root_user));
    }

    char *connection_timeout = config_get_value(obj->parameters, "connection_timeout");
    if (connection_timeout)
    {
        serviceSetTimeout(obj->element, atoi(connection_timeout));
    }

    const char *max_connections = config_get_value_string(obj->parameters, "max_connections");
    const char *max_queued_connections = config_get_value_string(obj->parameters, "max_queued_connections");
    const char *queued_connection_timeout = config_get_value_string(obj->parameters, "queued_connection_timeout");
    if (strlen(max_connections))
    {
        serviceSetConnectionLimits(obj->element, atoi(max_connections),
                                   atoi(max_queued_connections), atoi(queued_connection_timeout));
    }

    char *auth_all_servers = config_get_value(obj->parameters, "auth_all_servers");
    if (auth_all_servers)
    {
        serviceAuthAllServers(obj->element, config_truth_value(auth_all_servers));
    }

    char *strip_db_esc = config_get_value(obj->parameters, "strip_db_esc");
    if (strip_db_esc)
    {
        serviceStripDbEsc(obj->element, config_truth_value(strip_db_esc));
    }

    char *weightby = config_get_value(obj->parameters, "weightby");
    if (weightby)
    {
        serviceWeightBy(obj->element, weightby);
    }

    char *wildcard = config_get_value(obj->parameters, "localhost_match_wildcard_host");
    if (wildcard)
    {
        serviceEnableLocalhostMatchWildcardHost(obj->element, config_truth_value(wildcard));
    }

    char *user = config_get_value(obj->parameters, "user");
    char *auth = config_get_password(obj->parameters);

    if (user && auth)
    {
        serviceSetUser(obj->element, user, auth);
    }
    else if (!is_internal_service(router))
    {
        error_count++;
        MXS_ERROR("Service '%s' is missing %s%s%s.",
                  obj->object,
                  user ? "" : "the 'user' parameter",
                  !user && !auth ? " and " : "",
                  auth ? "" : "the 'password' or 'passwd' parameter");
    }

    char *log_auth_warnings = config_get_value(obj->parameters, "log_auth_warnings");
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

    char *version_string = config_get_value(obj->parameters, "version_string");
    if (version_string)
    {
        /** Add the 5.5.5- string to the start of the version string if
         * the version string starts with "10.".
         * This mimics MariaDB 10.0 replication which adds 5.5.5- for backwards compatibility. */
        if (version_string[0] != '5')
        {
            size_t len = strlen(version_string) + strlen("5.5.5-") + 1;
            service->version_string = MXS_MALLOC(len);
            MXS_ABORT_IF_NULL(service->version_string);
            strcpy(service->version_string, "5.5.5-");
            strcat(service->version_string, version_string);
        }
        else
        {
            service->version_string = MXS_STRDUP_A(version_string);
        }
    }
    else
    {
        if (gateway.version_string)
        {
            service->version_string = MXS_STRDUP_A(gateway.version_string);
        }
    }


    /** Store the configuration parameters for the service */
    const MXS_MODULE *mod = get_module(router, MODULE_ROUTER);

    if (mod)
    {
        config_add_defaults(obj, mod->parameters);
        service_add_parameters(obj->element, obj->parameters);
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
    char *address = config_get_value(obj->parameters, "address");
    char *port = config_get_value(obj->parameters, "port");
    char *protocol = config_get_value(obj->parameters, "protocol");
    char *monuser = config_get_value(obj->parameters, "monitoruser");
    char *monpw = config_get_value(obj->parameters, "monitorpw");
    char *auth = config_get_value(obj->parameters, "authenticator");
    char *auth_opts = config_get_value(obj->parameters, "authenticator_options");

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
        SERVER *server = obj->element;

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
        const char *poolmax = config_get_value_string(obj->parameters, "persistpoolmax");
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

        const char *persistmax = config_get_value_string(obj->parameters, "persistmaxtime");
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
                server_add_parameter(obj->element, params->name, params->value);
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
    char *filters = config_get_value(obj->parameters, "filters");
    char *servers = config_get_value(obj->parameters, "servers");
    char *monitor = config_get_value(obj->parameters, "monitor");
    char *roptions = config_get_value(obj->parameters, "router_options");
    SERVICE *service = obj->element;

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
                    servers = config_get_value(ctx->parameters, "servers");
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
                        serviceAddBackend(service, obj1->element);
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

    char *module = config_get_value(obj->parameters, "module");
    if (module)
    {
        if ((obj->element = monitor_alloc(obj->object, module)) == NULL)
        {
            MXS_ERROR("Failed to create monitor '%s'.", obj->object);
            error_count++;
        }

        if (obj->was_persisted)
        {
            /** Not the cleanest way of figuring out whether the configuration
             * was stored but it should be OK for now */
            ((MXS_MONITOR*)obj->element)->created_online = true;
        }
    }
    else
    {
        obj->element = NULL;
        MXS_ERROR("Monitor '%s' is missing the required 'module' parameter.", obj->object);
        error_count++;
    }

    char *servers = config_get_value(obj->parameters, "servers");

    if (error_count == 0)
    {
        const MXS_MODULE *mod = get_module(module, MODULE_MONITOR);

        if (mod)
        {
            config_add_defaults(obj, mod->parameters);
            monitorAddParameters(obj->element, obj->parameters);
        }
        else
        {
            error_count++;
        }

        char *interval_str = config_get_value(obj->parameters, "monitor_interval");
        if (interval_str)
        {
            char *endptr;
            long interval = strtol(interval_str, &endptr, 0);
            /* The interval must be >0 because it is used as a divisor.
                Perhaps a greater minimum value should be added? */
            if (*endptr == '\0' && interval > 0)
            {
                monitorSetInterval(obj->element, (unsigned long)interval);
            }
            else
            {
                MXS_NOTICE("Invalid 'monitor_interval' parameter for monitor '%s', "
                           "using default value of %d milliseconds.",
                           obj->object, MONITOR_DEFAULT_INTERVAL);
            }
        }
        else
        {
            MXS_NOTICE("Monitor '%s' is missing the 'monitor_interval' parameter, "
                       "using default value of %d milliseconds.",
                       obj->object, MONITOR_DEFAULT_INTERVAL);
        }

        char *connect_timeout = config_get_value(obj->parameters, "backend_connect_timeout");
        if (connect_timeout)
        {
            if (!monitorSetNetworkTimeout(obj->element, MONITOR_CONNECT_TIMEOUT, atoi(connect_timeout)))
            {
                MXS_ERROR("Failed to set backend_connect_timeout");
                error_count++;
            }
        }

        char *read_timeout = config_get_value(obj->parameters, "backend_read_timeout");
        if (read_timeout)
        {
            if (!monitorSetNetworkTimeout(obj->element, MONITOR_READ_TIMEOUT, atoi(read_timeout)))
            {
                MXS_ERROR("Failed to set backend_read_timeout");
                error_count++;
            }
        }

        char *write_timeout = config_get_value(obj->parameters, "backend_write_timeout");
        if (write_timeout)
        {
            if (!monitorSetNetworkTimeout(obj->element, MONITOR_WRITE_TIMEOUT, atoi(write_timeout)))
            {
                MXS_ERROR("Failed to set backend_write_timeout");
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
                        if (hashtable_add(monitorhash, obj1->object, "") == 0)
                        {
                            MXS_WARNING("Multiple monitors are monitoring server [%s]. "
                                        "This will cause undefined behavior.",
                                        obj1->object);
                        }
                        monitorAddServer(obj->element, obj1->element);
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

        char *user = config_get_value(obj->parameters, "user");
        char *passwd = config_get_password(obj->parameters);
        if (user && passwd)
        {
            monitorAddUser(obj->element, user, passwd);
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
    char *service_name = config_get_value(obj->parameters, "service");
    char *port = config_get_value(obj->parameters, "port");
    char *address = config_get_value(obj->parameters, "address");
    char *protocol = config_get_value(obj->parameters, "protocol");
    char *socket = config_get_value(obj->parameters, "socket");
    char *authenticator = config_get_value(obj->parameters, "authenticator");
    char *authenticator_options = config_get_value(obj->parameters, "authenticator_options");

    if (service_name && protocol && (socket || port))
    {
        SERVICE *service = service_find(service_name);
        if (service)
        {
            SSL_LISTENER *ssl_info = make_ssl_structure(obj, true, &error_count);
            if (socket)
            {
                if (serviceHasListener(service, protocol, address, 0))
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
                if (serviceHasListener(service, protocol, address, atoi(port)))
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
    char *module = config_get_value(obj->parameters, "module");

    if (module)
    {
        if ((obj->element = filter_alloc(obj->object, module)))
        {
            char *options = config_get_value(obj->parameters, "options");
            if (options)
            {
                char *lasts;
                char *s = strtok_r(options, ",", &lasts);
                while (s)
                {
                    filter_add_option(obj->element, s);
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
                filter_add_parameter(obj->element, p->name, p->value);
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

    return config_get_param(param, "ssl") &&
           config_get_param(param, "ssl_key") &&
           config_get_param(param, "ssl_cert") &&
           config_get_param(param, "ssl_ca_cert") &&
           strcmp(config_get_value_string(param, "ssl"), "required") == 0;
}

bool config_is_ssl_parameter(const char *key)
{
    const char *ssl_params[] =
    {
        "ssl_cert",
        "ssl_ca_cert",
        "ssl",
        "ssl_key",
        "ssl_version",
        "ssl_cert_verify_depth",
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

        if (params->options & MXS_MODULE_OPT_PATH_W_OK)
        {
            mode |= W_OK;
        }
        if (params->options & MXS_MODULE_OPT_PATH_R_OK)
        {
            mode |= R_OK;
        }
        if (params->options & MXS_MODULE_OPT_PATH_X_OK)
        {
            mode |= X_OK;
        }

        if (access(buf, mode) == 0)
        {
            valid = true;
        }
        else
        {
            char err[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Bad path parameter '%s' (absolute path '%s'): %d, %s", value,
                      buf, errno, strerror_r(errno, err, sizeof(err)));
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
            strcmp(type, config_get_value_string(ctx->parameters, "type")) == 0)
        {
            return true;
        }

        ctx = ctx->next;
    }

    return false;
}

bool config_param_is_valid(const MXS_MODULE_PARAM *params, const char *key,
                           const char *value, const CONFIG_CONTEXT *context)
{
    bool valid = false;

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
                if ((context && config_contains_type(context, value, "service")) ||
                    service_find(value))
                {
                    valid = true;
                }
                break;

            case MXS_MODULE_PARAM_SERVER:
                if ((context && config_contains_type(context, value, "server")) ||
                    server_find_by_unique_name(value))
                {
                    valid = true;
                }
                break;

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
