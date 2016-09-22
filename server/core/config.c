/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
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
#include <my_config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <ini.h>
#include <maxconfig.h>
#include <dcb.h>
#include <session.h>
#include <service.h>
#include <server.h>
#include <users.h>
#include <monitor.h>
#include <modules.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <mysql.h>
#include <sys/utsname.h>
#include <sys/fcntl.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <housekeeper.h>
#include <notification.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/utsname.h>
#include <dbusers.h>
#include <gw.h>
#include <maxscale/alloc.h>
#include <maxscale/limits.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

extern int setipaddress(struct in_addr *, char *);
static bool process_config_context(CONFIG_CONTEXT   *);
static int process_config_update(CONFIG_CONTEXT *);
static void free_config_context(CONFIG_CONTEXT      *);
static char *config_get_value(CONFIG_PARAMETER *, const char *);
static char *config_get_password(CONFIG_PARAMETER *);
static const char *config_get_value_string(CONFIG_PARAMETER *, const char *);
static int handle_global_item(const char *, const char *);
static int handle_feedback_item(const char *, const char *);
static void global_defaults();
static void feedback_defaults();
static bool check_config_objects(CONFIG_CONTEXT *context);
static int maxscale_getline(char** dest, int* size, FILE* file);
static SSL_LISTENER *make_ssl_structure(CONFIG_CONTEXT *obj, bool require_cert, int *error_count);

int config_truth_value(char *str);
int config_get_ifaddr(unsigned char *output);
static int config_get_release_string(char* release);
FEEDBACK_CONF *config_get_feedback_data();
void config_add_param(CONFIG_CONTEXT*, char*, char*);
bool config_has_duplicate_sections(const char* config);
int create_new_service(CONFIG_CONTEXT *obj);
int create_new_server(CONFIG_CONTEXT *obj);
int create_new_monitor(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj, HASHTABLE* monitorhash);
int create_new_listener(CONFIG_CONTEXT *obj, bool startnow);
int create_new_filter(CONFIG_CONTEXT *obj);
int configure_new_service(CONFIG_CONTEXT *context, CONFIG_CONTEXT *obj);

static char          *config_file = NULL;
static GATEWAY_CONF  gateway;
static FEEDBACK_CONF feedback;
char                 *version_string = NULL;


static char *service_params[] =
{
    "type",
    "router",
    "router_options",
    "servers",
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
    "max_slave_connections",
    "max_slave_replication_lag",
    "use_sql_variables_in",         /*< rwsplit only */
    "subservices",
    "version_string",
    "filters",
    "weightby",
    "ignore_databases",
    "ignore_databases_regex",
    "log_auth_warnings",
    "source", /**< Avrorouter only */
    "retry_on_failure",
    NULL
};

static char *listener_params[] =
{
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
    NULL
};

static char *monitor_params[] =
{
    "type",
    "module",
    "servers",
    "user",
    "passwd",   // DEPRECATE: See config_get_password.
    "password",
    "script",
    "events",
    "mysql51_replication",
    "monitor_interval",
    "detect_replication_lag",
    "detect_stale_master",
    "disable_master_failback",
    "backend_connect_timeout",
    "backend_read_timeout",
    "backend_write_timeout",
    "available_when_donor",
    "disable_master_role_setting",
    "use_priority",
    "multimaster",
    "failover",
    "failcount",
    NULL
};

static char *server_params[] =
{
    "type",
    "protocol",
    "port",
    "address",
    "authenticator",
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
    NULL
};

/**
 * Remove extra commas and whitespace from a string. This string is interpreted
 * as a list of string values separated by commas.
 * @param strptr String to clean
 * @return pointer to a new string or NULL if an error occurred
 */
char* config_clean_string_list(char* str)
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
            PCRE2_UCHAR errbuf[STRERROR_BUFLEN];
            pcre2_get_error_message(re_err, errbuf, sizeof(errbuf));
            MXS_ERROR("[%s] Regular expression compilation failed at %d: %s",
                      __FUNCTION__, (int)err_offset, errbuf);
            pcre2_code_free(re);
            MXS_FREE(dest);
            return NULL;
        }

        const char *replace = "$1,";
        int rval = 0;
        while ((rval = pcre2_substitute(re, (PCRE2_SPTR) str, PCRE2_ZERO_TERMINATED, 0,
                                        PCRE2_SUBSTITUTE_GLOBAL, data, NULL,
                                        (PCRE2_SPTR) replace, PCRE2_ZERO_TERMINATED,
                                        (PCRE2_UCHAR*) dest, &destsize)) == PCRE2_ERROR_NOMEMORY)
        {
            char* tmp = MXS_REALLOC(dest, destsize * 2);
            if (tmp == NULL)
            {
                MXS_FREE(dest);
                dest = NULL;
                break;
            }
            dest = tmp;
            destsize *= 2;
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
handler(void *userdata, const char *section, const char *name, const char *value)
{
    CONFIG_CONTEXT   *cntxt = (CONFIG_CONTEXT *)userdata;
    CONFIG_CONTEXT   *ptr = cntxt;
    CONFIG_PARAMETER *param, *p1;

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
        if ((ptr = (CONFIG_CONTEXT *)MXS_MALLOC(sizeof(CONFIG_CONTEXT))) == NULL)
        {
            return 0;
        }

        ptr->object = MXS_STRDUP_A(section);
        ptr->parameters = NULL;
        ptr->next = cntxt->next;
        ptr->element = NULL;
        cntxt->next = ptr;
    }
    /* Check to see if the parameter already exists for the section */
    p1 = ptr->parameters;
    while (p1)
    {
        if (!strcmp(p1->name, name))
        {
            char *tmp;
            int paramlen = strlen(p1->value) + strlen(value) + 2;

            if ((tmp = MXS_REALLOC(p1->value, sizeof(char) * (paramlen))) == NULL)
            {
                return 0;
            }
            strcat(tmp, ",");
            strcat(tmp, value);
            if ((p1->value = config_clean_string_list(tmp)) == NULL)
            {
                p1->value = tmp;
                MXS_ERROR("[%s] Cleaning configuration parameter failed.", __FUNCTION__);
                return 0;
            }
            MXS_FREE(tmp);
            return 1;
        }
        p1 = p1->next;
    }

    if ((param = (CONFIG_PARAMETER *)MXS_MALLOC(sizeof(CONFIG_PARAMETER))) == NULL)
    {
        return 0;
    }

    param->name = MXS_STRDUP_A(name);
    param->value = MXS_STRDUP_A(value);
    param->next = ptr->parameters;
    param->qfd_param_type = UNDEFINED_TYPE;
    ptr->parameters = param;

    return 1;
}

/**
 * @brief Load the configuration file for the MaxScale
 *
 * This function will parse the configuration file, check for duplicate sections,
 * validate the module parameters and finally turn it into a set of objects.
 *
 * @param file The filename of the configuration file
 * @return True on success, false on fatal error
 */
bool
config_load(char *file)
{
    CONFIG_CONTEXT config = {.object = ""};
    int ini_rval;
    bool rval = false;

    if (config_has_duplicate_sections(file))
    {
        return false;
    }

    /* Temporary - should use configuration values and test return value (bool) */
    dcb_pre_alloc(1000);
    session_pre_alloc(250);

    global_defaults();
    feedback_defaults();

    if ((ini_rval = ini_parse(file, handler, &config)) != 0)
    {
        char errorbuffer[1024 + 1];

        if (ini_rval > 0)
        {
            snprintf(errorbuffer, sizeof(errorbuffer),
                     "Error: Failed to parse configuration file. Error on line %d.", ini_rval);
        }
        else if (ini_rval == -1)
        {
            snprintf(errorbuffer, sizeof(errorbuffer),
                     "Error: Failed to parse configuration file. Failed to open file.");
        }
        else
        {
            snprintf(errorbuffer, sizeof(errorbuffer),
                     "Error: Failed to parse configuration file. Memory allocation failed.");
        }

        MXS_ERROR("%s", errorbuffer);
        return 0;
    }

    config_file = file;

    if (check_config_objects(config.next) && process_config_context(config.next))
    {
        rval = true;
    }

    free_config_context(config.next);
    return rval;
}

/**
 * Reload the configuration file for the MaxScale
 *
 * @return A zero return indicates a fatal error reading the configuration
 */
int
config_reload()
{
    CONFIG_CONTEXT  config;
    int             rval;

    if (!config_file)
    {
        return 0;
    }

    if (config_has_duplicate_sections(config_file))
    {
        return 0;
    }

    if (gateway.version_string)
    {
        MXS_FREE(gateway.version_string);
    }

    global_defaults();

    config.object = "";
    config.next = NULL;

    if (ini_parse(config_file, handler, &config) < 0)
    {
        return 0;
    }

    rval = process_config_update(config.next);
    free_config_context(config.next);

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
                    error_count += create_new_listener(obj, false);
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
config_get_value(CONFIG_PARAMETER *params, const char *name)
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
config_get_password(CONFIG_PARAMETER *params)
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
static const char *
config_get_value_string(CONFIG_PARAMETER *params, const char *name)
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


CONFIG_PARAMETER* config_get_param(
    CONFIG_PARAMETER* params,
    const char*       name)
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

config_param_type_t config_get_paramtype(
    CONFIG_PARAMETER* param)
{
    return param->qfd_param_type;
}

bool config_get_valint(
    int*                val,
    CONFIG_PARAMETER*   param,
    const char*         name, /*< if NULL examine current param only */
    config_param_type_t ptype)
{
    bool succp = false;;

    ss_dassert((ptype == COUNT_TYPE || ptype == PERCENT_TYPE) && param != NULL);

    while (param)
    {
        if (name == NULL || !strncmp(param->name, name, MAX_PARAM_LEN))
        {
            switch (ptype)
            {
                case COUNT_TYPE:
                    *val = param->qfd.valcount;
                    succp = true;
                    goto return_succp;

                case PERCENT_TYPE:
                    *val = param->qfd.valpercent;
                    succp  = true;
                    goto return_succp;

                default:
                    goto return_succp;
            }
        }
        param = param->next;
    }
return_succp:
    return succp;
}


bool config_get_valbool(
    bool*               val,
    CONFIG_PARAMETER*   param,
    const char*         name,
    config_param_type_t ptype)
{
    bool succp;

    ss_dassert(ptype == BOOL_TYPE);
    ss_dassert(param != NULL);

    if (ptype != BOOL_TYPE || param == NULL)
    {
        succp = false;
        goto return_succp;
    }

    while (param)
    {
        if (name == NULL || !strncmp(param->name, name, MAX_PARAM_LEN))
        {
            *val = param->qfd.valbool;
            succp = true;
            goto return_succp;
        }
        param = param->next;
    }
    succp = false;

return_succp:
    return succp;
}


bool config_get_valtarget(
    target_t*           val,
    CONFIG_PARAMETER*   param,
    const char*         name,
    config_param_type_t ptype)
{
    bool succp;

    ss_dassert(ptype == SQLVAR_TARGET_TYPE);
    ss_dassert(param != NULL);

    if (ptype != SQLVAR_TARGET_TYPE || param == NULL)
    {
        succp = false;
        goto return_succp;
    }

    while (param)
    {
        if (name == NULL || !strncmp(param->name, name, MAX_PARAM_LEN))
        {
            *val = param->qfd.valtarget;
            succp = true;
            goto return_succp;
        }
        param = param->next;
    }
    succp = false;

return_succp:
    return succp;
}

CONFIG_PARAMETER* config_clone_param(
    CONFIG_PARAMETER* param)
{
    CONFIG_PARAMETER* p2;

    p2 = (CONFIG_PARAMETER*) MXS_MALLOC(sizeof(CONFIG_PARAMETER));

    if (p2 == NULL)
    {
        goto return_p2;
    }
    memcpy(p2, param, sizeof(CONFIG_PARAMETER));
    p2->name = strndup(param->name, MAX_PARAM_LEN);
    p2->value = strndup(param->value, MAX_PARAM_LEN);

    if (param->qfd_param_type == STRING_TYPE)
    {
        p2->qfd.valstr = strndup(param->qfd.valstr, MAX_PARAM_LEN);
    }

return_p2:
    return p2;
}

/**
 * Free a configuration parameter
 * @param p1 Parameter to free
 */
void free_config_parameter(CONFIG_PARAMETER* p1)
{
    while (p1)
    {
        MXS_FREE(p1->name);
        MXS_FREE(p1->value);
        CONFIG_PARAMETER* p2 = p1->next;
        MXS_FREE(p1);
        p1 = p2;
    }
}

/**
 * Free a config tree
 *
 * @param context       The configuration data
 */
static  void
free_config_context(CONFIG_CONTEXT *context)
{
    CONFIG_CONTEXT   *obj;
    CONFIG_PARAMETER *p1, *p2;

    while (context)
    {
        MXS_FREE(context->object);
        free_config_parameter(context->parameters);
        obj = context->next;
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

                mxs_log_set_priority_enabled(lognames[i].priority, config_truth_value((char*)value));
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
static SSL_LISTENER *
make_ssl_structure (CONFIG_CONTEXT *obj, bool require_cert, int *error_count)
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
            ssl_cert_verify_depth = config_get_value(obj->parameters, "ssl_cert_verify_depth");
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
        feedback.feedback_enable = config_truth_value((char *)value);
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
static  int
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

                    CONFIG_PARAMETER* param;

                    if ((param = config_get_param(obj->parameters, "ignore_databases")))
                    {
                        service_set_param_value(service, param, param->value, 0, STRING_TYPE);
                    }

                    if ((param = config_get_param(obj->parameters, "ignore_databases_regex")))
                    {
                        service_set_param_value(service, param, param->value, 0, STRING_TYPE);
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
                            serviceSetTimeout(service, config_truth_value(connection_timeout));
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
                            service_set_param_value(service,
                                                    config_get_param(obj->parameters, "auth_all_servers"),
                                                    auth_all_servers, 0, BOOL_TYPE);
                        }

                        if (strip_db_esc)
                        {
                            serviceStripDbEsc(service, config_truth_value(strip_db_esc));
                        }

                        if (allow_localhost_match_wildcard_host)
                            serviceEnableLocalhostMatchWildcardHost(
                                service,
                                config_truth_value(allow_localhost_match_wildcard_host));

                        /** Read, validate and set max_slave_connections */
                        max_slave_conn_str =
                            config_get_value(obj->parameters, "max_slave_connections");

                        if (max_slave_conn_str != NULL)
                        {
                            CONFIG_PARAMETER* param;
                            bool              succp;

                            param = config_get_param(obj->parameters,
                                                     "max_slave_connections");

                            if (param == NULL)
                            {
                                succp = false;
                            }
                            else
                            {
                                succp = service_set_param_value(service, param,
                                                                max_slave_conn_str,
                                                                COUNT_ATMOST,
                                                                (PERCENT_TYPE | COUNT_TYPE));
                            }

                            if (!succp && param != NULL)
                            {
                                MXS_WARNING("Invalid value type "
                                            "for parameter \'%s.%s = %s\'\n\tExpected "
                                            "type is either <int> for slave connection "
                                            "count or\n\t<int>%% for specifying the "
                                            "maximum percentage of available the "
                                            "slaves that will be connected.",
                                            service->name,
                                            param->name,
                                            param->value);
                            }
                        }
                        /** Read, validate and set max_slave_replication_lag */
                        max_slave_rlag_str =
                            config_get_value(obj->parameters, "max_slave_replication_lag");

                        if (max_slave_rlag_str != NULL)
                        {
                            CONFIG_PARAMETER* param;
                            bool              succp;

                            param = config_get_param(obj->parameters,
                                                     "max_slave_replication_lag");

                            if (param == NULL)
                            {
                                succp = false;
                            }
                            else
                            {
                                succp = service_set_param_value(service,
                                                                param,
                                                                max_slave_rlag_str,
                                                                COUNT_ATMOST,
                                                                COUNT_TYPE);
                            }

                            if (!succp)
                            {
                                if (param)
                                {
                                    MXS_WARNING("Invalid value type "
                                                "for parameter \'%s.%s = %s\'\n\tExpected "
                                                "type is <int> for maximum "
                                                "slave replication lag.",
                                                service->name,
                                                param->name,
                                                param->value);
                                }
                                else
                                {
                                    MXS_ERROR("Parameter was NULL");
                                }
                            }
                        }
                    }

                    obj->element = service;
                }
                else
                {
                    MXS_NOTICE("New services can't be started while MaxScale is running."
                               " Please restart MaxScale to start the new services.");
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
                char *protocol = config_get_value(obj->parameters, "protocol");
                char *monuser = config_get_value(obj->parameters, "monuser");
                char *monpw = config_get_value(obj->parameters, "monpw");
                server_update(server, protocol, monuser, monpw);
                obj->element = server;
            }
            else
            {
                create_new_server(obj);
            }
        }
        obj = obj->next;
    }

    return 1;
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
    CONFIG_CONTEXT   *obj;
    CONFIG_PARAMETER *params;
    char             *type, **param_set;
    bool              rval = true;

    obj = context;
    while (obj)
    {
        param_set = NULL;
        if (obj->parameters && (type = config_get_value(obj->parameters, "type")))
        {
            if (!strcmp(type, "service"))
            {
                param_set = service_params;
            }
            else if (!strcmp(type, "listener"))
            {
                param_set = listener_params;
            }
            else if (!strcmp(type, "monitor"))
            {
                param_set = monitor_params;
            }
        }

        if (param_set != NULL)
        {
            params = obj->parameters;
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
                    MXS_ERROR("Unexpected parameter '%s' for object '%s' of type '%s'.",
                              params->name, obj->object, type);
                    rval = false;
                }
                params = params->next;
            }
        }
        obj = obj->next;
    }

    return rval;
}

/**
 * Set qualified parameter value to CONFIG_PARAMETER struct.
 */
bool config_set_qualified_param(CONFIG_PARAMETER* param,
                                void* val,
                                config_param_type_t type)
{
    bool succp;

    switch (type)
    {
        case STRING_TYPE:
            param->qfd.valstr = strndup((const char *)val, MAX_PARAM_LEN);
            succp = true;
            break;

        case COUNT_TYPE:
            param->qfd.valcount = *(int *)val;
            succp = true;
            break;

        case PERCENT_TYPE:
            param->qfd.valpercent = *(int *)val;
            succp = true;
            break;

        case BOOL_TYPE:
            param->qfd.valbool = *(bool *)val;
            succp = true;
            break;

        case SQLVAR_TARGET_TYPE:
            param->qfd.valtarget = *(target_t *)val;
            succp = true;
            break;
        default:
            succp = false;
            break;
    }

    if (succp)
    {
        param->qfd_param_type = type;
    }
    return succp;
}

/**
 * Used for boolean settings where values may be 1, yes or true
 * to enable a setting or -, no, false to disable a setting.
 *
 * @param       str     String to convert to a boolean
 * @return      Truth value
 */
int
config_truth_value(char *str)
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
    MXS_ERROR("Not a boolean value: %s", str);
    return -1;
}


/**
 * Converts a string into a floating point representation of a percentage value.
 * For example 75% is converted to 0.75 and -10% is converted to -0.1.
 * @param       str     String to convert
 * @return      String converted to a floating point percentage
 */
double
config_percentage_value(char *str)
{
    double value = 0;

    value = strtod(str, NULL);
    if (value != 0)
    {
        value /= 100.0;
    }

    return value;
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

void config_add_param(CONFIG_CONTEXT* obj, char* key, char* value)
{
    key = MXS_STRDUP(key);
    value = MXS_STRDUP(value);

    CONFIG_PARAMETER* param = (CONFIG_PARAMETER *)MXS_MALLOC(sizeof(CONFIG_PARAMETER));

    if (!key || !value || !param)
    {
        MXS_FREE(key);
        MXS_FREE(value);
        MXS_FREE(param);
        return;
    }

    param->name = key;
    param->value = value;
    param->next = obj->parameters;
    obj->parameters = param;
}
/**
 * Return the pointer to the global options for MaxScale.
 * @return Pointer to the GATEWAY_CONF structure. This is a static structure and
 * should not be modified.
 */
GATEWAY_CONF* config_get_global_options()
{
    return &gateway;
}

/**
 * Check if sections are defined multiple times in the configuration file.
 * @param config Path to the configuration file
 * @return True if duplicate sections were found or an error occurred
 */
bool config_has_duplicate_sections(const char* config)
{
    bool rval = false;
    const int table_size = 10;
    int errcode;
    PCRE2_SIZE erroffset;
    HASHTABLE *hash = hashtable_alloc(table_size, hashtable_item_strhash, hashtable_item_strcmp);
    pcre2_code *re = pcre2_compile((PCRE2_SPTR) "^\\s*\\[(.+)\\]\\s*$", PCRE2_ZERO_TERMINATED,
                                   0, &errcode, &erroffset, NULL);
    pcre2_match_data *mdata = NULL;
    int size = 1024;
    char *buffer = MXS_MALLOC(size * sizeof(char));

    if (buffer && hash && re && (mdata = pcre2_match_data_create_from_pattern(re, NULL)))
    {
        hashtable_memory_fns(hash, hashtable_item_strdup, NULL, hashtable_item_free, NULL);
        FILE* file = fopen(config, "r");

        if (file)
        {
            while (maxscale_getline(&buffer, &size, file) > 0)
            {
                if (pcre2_match(re, (PCRE2_SPTR) buffer,
                                PCRE2_ZERO_TERMINATED, 0, 0,
                                mdata, NULL) > 0)
                {
                    /**
                     * Neither of the PCRE2 calls will fail since we know the pattern
                     * beforehand and we allocate enough memory from the stack
                     */
                    PCRE2_SIZE len;
                    pcre2_substring_length_bynumber(mdata, 1, &len);
                    len += 1; /** one for the null terminator */
                    PCRE2_UCHAR section[len];
                    pcre2_substring_copy_bynumber(mdata, 1, section, &len);

                    if (hashtable_add(hash, section, "") == 0)
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
            char errbuf[STRERROR_BUFLEN];
            MXS_ERROR("Failed to open file '%s': %s", config,
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

    hashtable_free(hash);
    pcre2_code_free(re);
    pcre2_match_data_free(mdata);
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
    CONFIG_PARAMETER* param;

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
        service_set_param_value(service,
                                config_get_param(obj->parameters, "auth_all_servers"),
                                auth_all_servers, 0, BOOL_TYPE);
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

    char *subservices = config_get_value(obj->parameters, "subservices");
    if (subservices)
    {
        service_set_param_value(obj->element, obj->parameters, subservices, 1, STRING_TYPE);
    }

    CONFIG_PARAMETER *src = config_get_param(obj->parameters, "source");
    if (src)
    {
        service_set_param_value(obj->element, src, src->value, 1, STRING_TYPE);
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

    if ((param = config_get_param(obj->parameters, "ignore_databases")))
    {
        service_set_param_value(obj->element, param, param->value, 0, STRING_TYPE);
    }

    if ((param = config_get_param(obj->parameters, "ignore_databases_regex")))
    {
        service_set_param_value(obj->element, param, param->value, 0, STRING_TYPE);
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

    /** Parameters for rwsplit router only */
    if (strcmp(router, "readwritesplit") == 0)
    {
        if ((param = config_get_param(obj->parameters, "max_slave_connections")))
        {
            if (!service_set_param_value(obj->element, param, param->value,
                                         COUNT_ATMOST, (COUNT_TYPE | PERCENT_TYPE)))
            {
                MXS_WARNING("Invalid value type for parameter \'%s.%s = %s\'\n\tExpected "
                            "type is either <int> for slave connection count or\n\t<int>%% for specifying the "
                            "maximum percentage of available the slaves that will be connected.",
                            service->name, param->name, param->value);
            }
        }

        if ((param = config_get_param(obj->parameters, "max_slave_replication_lag")))
        {
            if (!service_set_param_value(obj->element, param, param->value,
                                         COUNT_ATMOST, COUNT_TYPE))
            {
                MXS_WARNING("Invalid value type for parameter \'%s.%s = %s\'\n\tExpected "
                            "type is <int> for maximum slave replication lag.",
                            service->name, param->name, param->value);
            }
        }

        if ((param = config_get_param(obj->parameters, "use_sql_variables_in")))
        {
            if (!service_set_param_value(obj->element, param, param->value,
                                         COUNT_NONE, SQLVAR_TARGET_TYPE))
            {
                MXS_WARNING("Invalid value type for parameter \'%s.%s = %s\'\n\tExpected "
                            "type is [master|all] for use sql variables in.",
                            service->name, param->name, param->value);
            }
        }
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

    if (address && port && protocol)
    {
        if ((obj->element = server_alloc(address, protocol, atoi(port))))
        {
            server_set_unique_name(obj->element, obj->object);
        }
        else
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
            serverAddMonUser(server, monuser, monpw);
        }
        else if (monuser && monpw == NULL)
        {
            MXS_ERROR("Server '%s' has a monitoruser defined but no corresponding "
                      "password.", obj->object);
            error_count++;
        }

        if (auth && (server->authenticator = MXS_STRDUP(auth)) == NULL)
        {
            error_count++;
        }

        char *endptr;
        const char *poolmax = config_get_value_string(obj->parameters, "persistpoolmax");
        if (poolmax)
        {
            server->persistpoolmax = strtol(poolmax, &endptr, 0);
            if (*endptr != '\0')
            {
                MXS_ERROR("Invalid value for 'persistpoolmax' for server %s: %s",
                          server->unique_name, poolmax);
            }
        }

        const char *persistmax = config_get_value_string(obj->parameters, "persistmaxtime");
        if (persistmax)
        {
            server->persistmaxtime = strtol(persistmax, &endptr, 0);
            if (*endptr != '\0')
            {
                MXS_ERROR("Invalid value for 'persistmaxtime' for server %s: %s",
                          server->unique_name, persistmax);
            }
        }

        CONFIG_PARAMETER *params = obj->parameters;

        server->server_ssl = make_ssl_structure(obj, false, &error_count);
        if (server->server_ssl && listener_init_SSL(server->server_ssl) != 0)
        {
            MXS_ERROR("Unable to initialize server SSL");
        }

        while (params)
        {
            if (!is_normal_server_parameter(params->name))
            {
                serverAddParameter(obj->element, params->name, params->value);
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
    char *roptions = config_get_value(obj->parameters, "router_options");
    char *router = config_get_value(obj->parameters, "router");
    SERVICE *service = obj->element;

    if (service)
    {
        if (servers)
        {
            char *lasts;
            char *s = strtok_r(servers, ",", &lasts);
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
        else if (servers == NULL && !is_internal_service(router))
        {
            MXS_ERROR("The service '%s' is missing a definition of the servers "
                      "that provide the service.", obj->object);
            error_count++;
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
    }
    else
    {
        obj->element = NULL;
        MXS_ERROR("Monitor '%s' is missing the require 'module' parameter.", obj->object);
        error_count++;
    }

    char *servers = config_get_value(obj->parameters, "servers");
    if (servers == NULL)
    {
        MXS_ERROR("Monitor '%s' is missing the 'servers' parameter that "
                  "lists the servers that it monitors.", obj->object);
        error_count++;
    }

    if (error_count == 0)
    {
        monitorAddParameters(obj->element, obj->parameters);

        char *interval = config_get_value(obj->parameters, "monitor_interval");
        if (interval)
        {
            monitorSetInterval(obj->element, atoi(interval));
        }
        else
        {
            MXS_NOTICE("Monitor '%s' is missing the 'monitor_interval' parameter, "
                       "using default value of 10000 milliseconds.", obj->object);
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
int create_new_listener(CONFIG_CONTEXT *obj, bool startnow)
{
    int error_count = 0;
    char *service_name = config_get_value(obj->parameters, "service");
    char *port = config_get_value(obj->parameters, "port");
    char *address = config_get_value(obj->parameters, "address");
    char *protocol = config_get_value(obj->parameters, "protocol");
    char *socket = config_get_value(obj->parameters, "socket");
    char *authenticator = config_get_value(obj->parameters, "authenticator");

    if (service_name && protocol && (socket || port))
    {
        SERVICE *service = service_find(service_name);
        if (service)
        {
            SSL_LISTENER *ssl_info = make_ssl_structure(obj, true, &error_count);
            if (socket)
            {
                if (serviceHasProtocol(service, protocol, address, 0))
                {
                    MXS_ERROR("Listener '%s' for service '%s' already has a socket at '%s.",
                              obj->object, service_name, socket);
                    error_count++;
                }
                else
                {
                    serviceAddProtocol(service, obj->object, protocol, socket, 0,
                                       authenticator, ssl_info);
                    if (startnow)
                    {
                        serviceStartProtocol(service, protocol, 0);
                    }
                }
            }

            if (port)
            {
                if (serviceHasProtocol(service, protocol, address, atoi(port)))
                {
                    MXS_ERROR("Listener '%s', for service '%s', already have port %s.",
                              obj->object,
                              service_name,
                              port);
                    error_count++;
                }
                else
                {
                    serviceAddProtocol(service, obj->object, protocol, address,
                                       atoi(port), authenticator, ssl_info);
                    if (startnow)
                    {
                        serviceStartProtocol(service, protocol, atoi(port));
                    }
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
                    filterAddOption(obj->element, s);
                    s = strtok_r(NULL, ",", &lasts);
                }
            }

            CONFIG_PARAMETER *params = obj->parameters;
            while (params)
            {
                if (strcmp(params->name, "module") && strcmp(params->name, "options"))
                {
                    filterAddParameter(obj->element, params->name, params->value);
                }
                params = params->next;
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
