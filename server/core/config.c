/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
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
 * 22/04/15 Martin Brampton     Added disable_master_role_setting parameter
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
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

extern int setipaddress(struct in_addr *, char *);
static int process_config_context(CONFIG_CONTEXT   *);
static int process_config_update(CONFIG_CONTEXT *);
static void free_config_context(CONFIG_CONTEXT      *);
static char *config_get_value(CONFIG_PARAMETER *, const char *);
static const char *config_get_value_string(CONFIG_PARAMETER *, const char *);
static int handle_global_item(const char *, const char *);
static int handle_feedback_item(const char *, const char *);
static void global_defaults();
static void feedback_defaults();
static void check_config_objects(CONFIG_CONTEXT *context);
static int maxscale_getline(char** dest, int* size, FILE* file);
int config_truth_value(char *str);
bool isInternalService(char *router);
int config_get_ifaddr(unsigned char *output);
int config_get_release_string(char* release);
FEEDBACK_CONF *config_get_feedback_data();
void config_add_param(CONFIG_CONTEXT*, char*, char*);
bool config_has_duplicate_sections(const char* config);
static char          *config_file = NULL;
static GATEWAY_CONF  gateway;
static FEEDBACK_CONF feedback;
char                 *version_string = NULL;


/**
 * Trim whitespace from the front and rear of a string
 *
 * @param str           String to trim
 * @return      Trimmed string, changes are done in situ
 */
static char *
trim(char *str)
{
    char *ptr;

    while (isspace(*str))
    {
        str++;
    }

    /* Point to last character of the string */
    ptr = str + strlen(str) - 1;
    while (ptr > str && isspace(*ptr))
    {
        *ptr-- = 0;
    }

    return str;
}

/**
 * Remove extra commas and whitespace from a string. This string is interpreted
 * as a list of string values separated by commas.
 * @param strptr String to clean
 * @return pointer to a new string or NULL if an error occurred
 */
char* config_clean_string_list(char* str)
{
    char *dest;
    size_t destsize = strlen(str) + 1;
    if ((dest = malloc(destsize)) != NULL)
    {
        pcre2_code* re;
        pcre2_match_data* data;
        int re_err;
        size_t err_offset;

        if ((re = pcre2_compile((PCRE2_SPTR) "[[:space:],]*([^,]*[^[:space:],])[[:space:],]*", PCRE2_ZERO_TERMINATED, 0,
                                &re_err, &err_offset, NULL)) == NULL ||
            (data = pcre2_match_data_create_from_pattern(re, NULL)) == NULL)
        {
            PCRE2_UCHAR errbuf[STRERROR_BUFLEN];
            pcre2_get_error_message(re_err, errbuf, sizeof(errbuf));
            MXS_ERROR("[%s] Regular expression compilation failed at %d: %s",
                      __FUNCTION__, (int)err_offset, errbuf);
            pcre2_code_free(re);
            free(dest);
            return NULL;
        }

        const char *replace = "$1,";
        int rval = 0;
        while ((rval = pcre2_substitute(re, (PCRE2_SPTR) str, PCRE2_ZERO_TERMINATED, 0,
                                        PCRE2_SUBSTITUTE_GLOBAL, data, NULL,
                                        (PCRE2_SPTR) replace, PCRE2_ZERO_TERMINATED,
                                        (PCRE2_UCHAR*) dest, &destsize)) == PCRE2_ERROR_NOMEMORY)
        {
            char* tmp = realloc(dest, destsize * 2);
            if (tmp == NULL)
            {
                free(dest);
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
    else
    {
        MXS_ERROR("[%s] Memory allocation failed.", __FUNCTION__);
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

    if (strcasecmp(section, "feedback") == 0)
    {
        return handle_feedback_item(name, value);
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
        if ((ptr = (CONFIG_CONTEXT *)malloc(sizeof(CONFIG_CONTEXT))) == NULL)
        {
            return 0;
        }

        ptr->object = strdup(section);
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

            if ((tmp = realloc(p1->value, sizeof(char) * (paramlen))) == NULL)
            {
                MXS_ERROR("[%s] Memory allocation failed.", __FUNCTION__);
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
            free(tmp);
            return 1;
        }
        p1 = p1->next;
    }

    if ((param = (CONFIG_PARAMETER *)malloc(sizeof(CONFIG_PARAMETER))) == NULL)
    {
        return 0;
    }

    param->name = strdup(name);
    param->value = strdup(value);
    param->next = ptr->parameters;
    param->qfd_param_type = UNDEFINED_TYPE;
    ptr->parameters = param;

    return 1;
}

/**
 * Load the configuration file for the MaxScale
 *
 * @param file  The filename of the configuration file
 * @return A zero return indicates a fatal error reading the configuration
 */
int
config_load(char *file)
{
    CONFIG_CONTEXT config;
    int            rval, ini_rval;

    if (config_has_duplicate_sections(file))
    {
        return 0;
    }
    MYSQL *conn;
    conn = mysql_init(NULL);
    if (conn)
    {
        if (mysql_real_connect(conn, NULL, NULL, NULL, NULL, 0, NULL, 0))
        {
            char *ptr, *tmp;

            tmp = (char *)mysql_get_server_info(conn);
            unsigned int server_version = mysql_get_server_version(conn);

            if (version_string)
            {
                free(version_string);
            }

            if ((version_string = malloc(strlen(tmp) + strlen("5.5.5-") + 1)) == NULL)
            {
                return 0;
            }

            if (server_version >= 100000)
            {
                strcpy(version_string, "5.5.5-");
                strcat(version_string, tmp);
            }
            else
            {
                strcpy(version_string, tmp);
            }

            ptr = strstr(version_string, "-embedded");
            if (ptr)
            {
                *ptr = '\0';
            }
        }
        mysql_close(conn);
    }

    global_defaults();
    feedback_defaults();

    config.object = "";
    config.next = NULL;

    if ((ini_rval = ini_parse(file, handler, &config)) != 0)
    {
        char errorbuffer[1024 + 1];

        if (ini_rval > 0)
        {
            snprintf(errorbuffer, sizeof(errorbuffer),
                     "Error: Failed to parse configuration file. Error on line %d.", ini_rval);
        }
        else if(ini_rval == -1)
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

    check_config_objects(config.next);
    rval = process_config_context(config.next);
    free_config_context(config.next);

    /** Start all monitors */
    if (rval)
    {
        monitorStartAll();
    }

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
        free(gateway.version_string);
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
 * Process a configuration context and turn it into the set of object
 * we need.
 *
 * @param context       The configuration data
 * @return A zero result indicates a fatal error
 */
static  int
process_config_context(CONFIG_CONTEXT *context)
{
    CONFIG_CONTEXT  *obj;
    int             error_count = 0;
    HASHTABLE*      monitorhash;

    if ((monitorhash = hashtable_alloc(5, simple_str_hash, strcmp)) == NULL)
    {
        MXS_ERROR("Failed to allocate, monitor configuration check hashtable.");
        return 0;
    }
    hashtable_memory_fns(monitorhash, (HASHMEMORYFN)strdup, NULL, (HASHMEMORYFN)free, NULL);

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
            MXS_ERROR("Configuration object '%s' has no type.", obj->object);
            error_count++;
        }
        else if (!strcmp(type, "service"))
        {
            char *router = config_get_value(obj->parameters, "router");
            if (router)
            {
                char* max_slave_conn_str;
                char* max_slave_rlag_str;
                char *user;
                char *auth;
                char *enable_root_user;
                char *connection_timeout;
                char *auth_all_servers;
                char *optimize_wildcard;
                char *strip_db_esc;
                char *weightby;
                char *version_string;
                char *subservices;
                char *ssl, *ssl_cert, *ssl_key, *ssl_ca_cert, *ssl_version;
                char* ssl_cert_verify_depth;
                bool  is_rwsplit = false;
                bool  is_schemarouter = false;
                char *allow_localhost_match_wildcard_host;

                obj->element = service_alloc(obj->object, router);
                user = config_get_value(obj->parameters, "user");
                auth = config_get_value(obj->parameters, "passwd");
                subservices = config_get_value(obj->parameters, "subservices");
                ssl = config_get_value(obj->parameters, "ssl");
                ssl_cert = config_get_value(obj->parameters, "ssl_cert");
                ssl_key = config_get_value(obj->parameters, "ssl_key");
                ssl_ca_cert = config_get_value(obj->parameters, "ssl_ca_cert");
                ssl_version = config_get_value(obj->parameters, "ssl_version");
                ssl_cert_verify_depth = config_get_value(obj->parameters, "ssl_cert_verify_depth");
                enable_root_user = config_get_value(obj->parameters, "enable_root_user");
                connection_timeout = config_get_value(obj->parameters, "connection_timeout");
                optimize_wildcard = config_get_value(obj->parameters, "optimize_wildcard");
                auth_all_servers = config_get_value(obj->parameters, "auth_all_servers");
                strip_db_esc = config_get_value(obj->parameters, "strip_db_esc");
                allow_localhost_match_wildcard_host = config_get_value(obj->parameters,
                                                                       "localhost_match_wildcard_host");
                weightby = config_get_value(obj->parameters, "weightby");

                version_string = config_get_value(obj->parameters, "version_string");

                if (subservices)
                {
                    service_set_param_value(obj->element,
                                            obj->parameters,
                                            subservices,
                                            1, STRING_TYPE);
                }
                char *log_auth_warnings = config_get_value(obj->parameters, "log_auth_warnings");
                int truthval;
                if (log_auth_warnings && (truthval = config_truth_value(log_auth_warnings)) != -1)
                {
                    ((SERVICE*) obj->element)->log_auth_warnings = (bool) truthval;
                }

                CONFIG_PARAMETER* param;
                if ((param = config_get_param(obj->parameters, "ignore_databases")))
                {
                    service_set_param_value(obj->element, param, param->value, 0, STRING_TYPE);
                }

                if ((param = config_get_param(obj->parameters, "ignore_databases_regex")))
                {
                    service_set_param_value(obj->element, param, param->value, 0, STRING_TYPE);
                }
                /** flag for rwsplit-specific parameters */
                if (strncmp(router, "readwritesplit", strlen("readwritesplit")+1) == 0)
                {
                    is_rwsplit = true;
                }

                allow_localhost_match_wildcard_host =
                    config_get_value(obj->parameters, "localhost_match_wildcard_host");

                if (obj->element == NULL) /*< if module load failed */
                {
                    MXS_ERROR("Reading configuration "
                              "for router service '%s' failed. "
                              "Router %s is not loaded.",
                              obj->object,
                              obj->object);
                    obj = obj->next;
                    continue; /*< process next obj */
                }

                if (version_string) {
                    /** Add the 5.5.5- string to the start of the version string if
                     * the version string starts with "10.".
                     * This mimics MariaDB 10.0 replication which adds 5.5.5- for backwards compatibility. */
                    if (strncmp(version_string, "10.", 3) == 0)
                    {
                        ((SERVICE *)(obj->element))->version_string =
                            malloc((strlen(version_string) + strlen("5.5.5-") + 1) * sizeof(char));
                        strcpy(((SERVICE *)(obj->element))->version_string, "5.5.5-");
                        strcat(((SERVICE *)(obj->element))->version_string, version_string);
                    }
                    else
                    {
                        ((SERVICE *)(obj->element))->version_string = strdup(version_string);
                    }
                }
                else
                {
                    if (gateway.version_string)
                        ((SERVICE *)(obj->element))->version_string = strdup(gateway.version_string);
                }

                max_slave_conn_str = config_get_value(obj->parameters, "max_slave_connections");
                max_slave_rlag_str = config_get_value(obj->parameters, "max_slave_replication_lag");

                if (ssl)
                {
                    if (ssl_cert == NULL)
                    {
                        error_count++;
                        MXS_ERROR("Server certificate missing for service '%s'."
                                  "Please provide the path to the server certificate by adding "
                                  "the ssl_cert=<path> parameter", obj->object);
                    }

                    if (ssl_ca_cert == NULL)
                    {
                        error_count++;
                        MXS_ERROR("CA Certificate missing for service '%s'."
                                  "Please provide the path to the certificate authority "
                                  "certificate by adding the ssl_ca_cert=<path> parameter",
                                  obj->object);
                    }

                    if (ssl_key == NULL)
                    {
                        error_count++;
                        MXS_ERROR("Server private key missing for service '%s'. "
                                  "Please provide the path to the server certificate key by "
                                  "adding the ssl_key=<path> parameter",
                                  obj->object);
                    }

                    if (access(ssl_ca_cert, F_OK) != 0)
                    {
                        MXS_ERROR("Certificate authority file for service '%s' "
                                  "not found: %s",
                                  obj->object, ssl_ca_cert);
                        error_count++;
                    }

                    if (access(ssl_cert, F_OK) != 0)
                    {
                        MXS_ERROR("Server certificate file for service '%s' not found: %s",
                                  obj->object,
                                  ssl_cert);
                        error_count++;
                    }

                    if (access(ssl_key, F_OK) != 0)
                    {
                        MXS_ERROR("Server private key file for service '%s' not found: %s",
                                  obj->object,
                                  ssl_key);
                        error_count++;
                    }

                    if (error_count == 0)
                    {
                        if (serviceSetSSL(obj->element, ssl) != 0)
                        {
                            MXS_ERROR("Unknown parameter for service '%s': %s",
                                      obj->object, ssl);
                            error_count++;
                        }
                        else
                        {
                            serviceSetCertificates(obj->element, ssl_cert, ssl_key, ssl_ca_cert);
                            if (ssl_version)
                            {
                                if (serviceSetSSLVersion(obj->element, ssl_version) != 0)
                                {
                                    MXS_ERROR("Unknown parameter value for "
                                              "'ssl_version' for service '%s': %s",
                                              obj->object, ssl_version);
                                    error_count++;
                                }
                            }
                            if (ssl_cert_verify_depth)
                            {
                                if (serviceSetSSLVerifyDepth(obj->element, atoi(ssl_cert_verify_depth)) != 0)
                                {
                                    MXS_ERROR("Invalid parameter value for "
                                              "'ssl_cert_verify_depth' for service '%s': %s",
                                              obj->object, ssl_cert_verify_depth);
                                    error_count++;
                                }
                            }
                        }
                    }
                }

                serviceSetRetryOnFailure(obj->element, config_get_value(obj->parameters, "retry_on_failure"));

                if (enable_root_user)
                {
                    serviceEnableRootUser(obj->element, config_truth_value(enable_root_user));
                }

                if (connection_timeout)
                {
                    serviceSetTimeout(obj->element, atoi(connection_timeout));
                }

                if(auth_all_servers)
                {
                    serviceAuthAllServers(obj->element, config_truth_value(auth_all_servers));
                }

                if(optimize_wildcard)
                {
                    serviceOptimizeWildcard(obj->element, config_truth_value(optimize_wildcard));
                }

                if(strip_db_esc)
                {
                    serviceStripDbEsc(obj->element, config_truth_value(strip_db_esc));
                }

                if (weightby)
                {
                    serviceWeightBy(obj->element, weightby);
                }

                if (allow_localhost_match_wildcard_host)
                {
                    serviceEnableLocalhostMatchWildcardHost(
                        obj->element,
                        config_truth_value(allow_localhost_match_wildcard_host));
                }

                if (!auth)
                {
                    auth = config_get_value(obj->parameters, "auth");
                }

                if (obj->element && user && auth)
                {
                    serviceSetUser(obj->element, user, auth);
                }
                else if (user && auth == NULL)
                {
                    MXS_ERROR("Service '%s' has a "
                              "user defined but no "
                              "corresponding password.",
                              obj->object);
                }
                /** Read, validate and set max_slave_connections */
                if (max_slave_conn_str != NULL)
                {
                    CONFIG_PARAMETER* param;
                    bool              succp;

                    param = config_get_param(obj->parameters, "max_slave_connections");

                    if (param == NULL)
                    {
                        succp = false;
                    }
                    else
                    {
                        succp = service_set_param_value(
                            obj->element,
                            param,
                            max_slave_conn_str,
                            COUNT_ATMOST,
                            (COUNT_TYPE|PERCENT_TYPE));
                    }

                    if (!succp)
                    {
                        MXS_WARNING("Invalid value type "
                                    "for parameter \'%s.%s = %s\'\n\tExpected "
                                    "type is either <int> for slave connection "
                                    "count or\n\t<int>%% for specifying the "
                                    "maximum percentage of available the "
                                    "slaves that will be connected.",
                                    ((SERVICE*)obj->element)->name,
                                    param->name,
                                    param->value);
                    }
                }
                /** Read, validate and set max_slave_replication_lag */
                if (max_slave_rlag_str != NULL)
                {
                    CONFIG_PARAMETER* param;
                    bool              succp;

                    param = config_get_param(
                        obj->parameters, 
                        "max_slave_replication_lag");

                    if (param == NULL)
                    {
                        succp = false;
                    }
                    else
                    {
                        succp = service_set_param_value(
                            obj->element,
                            param,
                            max_slave_rlag_str,
                            COUNT_ATMOST,
                            COUNT_TYPE);
                    }

                    if (!succp)
                    {
                        MXS_WARNING("Invalid value type "
                                    "for parameter \'%s.%s = %s\'\n\tExpected "
                                    "type is <int> for maximum "
                                    "slave replication lag.",
                                    ((SERVICE*)obj->element)->name,
                                    param->name,
                                    param->value);
                    }
                }
                /** Parameters for rwsplit router only */
                if (is_rwsplit)
                {
                    CONFIG_PARAMETER* param;
                    char*             use_sql_variables_in;
                    bool              succp;

                    use_sql_variables_in =
                        config_get_value(obj->parameters, "use_sql_variables_in");

                    if (use_sql_variables_in != NULL)
                    {
                        param = config_get_param(obj->parameters,
                                                 "use_sql_variables_in");

                        if (param == NULL)
                        {
                            succp = false;
                        }
                        else
                        {
                            succp = service_set_param_value(obj->element,
                                                            param,
                                                            use_sql_variables_in,
                                                            COUNT_NONE,
                                                            SQLVAR_TARGET_TYPE);
                        }

                        if (!succp)
                        {
                            if(param)
                            {
                                MXS_WARNING("Invalid value type "
                                            "for parameter \'%s.%s = %s\'\n\tExpected "
                                            "type is [master|all] for "
                                            "use sql variables in.",
                                            ((SERVICE*)obj->element)->name,
                                            param->name,
                                            param->value);
                            }
                            else
                            {
                                MXS_ERROR("Parameter was NULL");
                            }
                        }
                    }
                } /*< if (rw_split) */
            } /*< if (router) */
            else
            {
                obj->element = NULL;
                MXS_ERROR("No router defined for service '%s'.", obj->object);
                error_count++;
            }
        }
        else if (!strcmp(type, "server"))
        {
            char *address;
            char *port;
            char *protocol;
            char *monuser;
            char *monpw;

            address = config_get_value(obj->parameters, "address");
            port = config_get_value(obj->parameters, "port");
            protocol = config_get_value(obj->parameters, "protocol");
            monuser = config_get_value(obj->parameters, "monitoruser");
            monpw = config_get_value(obj->parameters, "monitorpw");

            if (address && port && protocol)
            {
                obj->element = server_alloc(address,
                                            protocol,
                                            atoi(port));
                server_set_unique_name(obj->element, obj->object);
            }
            else
            {
                obj->element = NULL;
                MXS_ERROR("Server '%s' is missing a "
                          "required configuration parameter. A "
                          "server must "
                          "have address, port and protocol "
                          "defined.",
                          obj->object);
                error_count++;
            }

            if (obj->element && monuser && monpw)
            {
                serverAddMonUser(obj->element, monuser, monpw);
            }
            else if (monuser && monpw == NULL)
            {
                MXS_ERROR("Server '%s' has a monitoruser"
                          "defined but no corresponding password.",
                          obj->object);
            }

            if (obj->element)
            {
                SERVER *server = obj->element;
                server->persistpoolmax = strtol(config_get_value_string(obj->parameters,
                                                                        "persistpoolmax"), NULL, 0);
                server->persistmaxtime = strtol(config_get_value_string(obj->parameters,
                                                                        "persistmaxtime"), NULL, 0);
                CONFIG_PARAMETER *params = obj->parameters;

                while (params)
                {
                    if (strcmp(params->name, "address")
                        && strcmp(params->name, "port")
                        && strcmp(params->name, "protocol")
                        && strcmp(params->name, "monitoruser")
                        && strcmp(params->name, "monitorpw")
                        && strcmp(params->name, "type")
                        && strcmp(params->name, "persistpoolmax")
                        && strcmp(params->name, "persistmaxtime"))
                    {
                        serverAddParameter(obj->element, params->name, params->value);
                    }
                    params = params->next;
                }
            }
        }
        else if (!strcmp(type, "filter"))
        {
            char *module = config_get_value(obj->parameters, "module");
            char *options = config_get_value(obj->parameters, "options");

            if (module)
            {
                obj->element = filter_alloc(obj->object, module);
            }
            else
            {
                MXS_ERROR("Filter '%s' has no module "
                          "defined defined to load.",
                          obj->object);
                error_count++;
            }

            if (obj->element && options)
            {
                char *lasts;
                char *s = strtok_r(options, ",", &lasts);
                while (s)
                {
                    filterAddOption(obj->element, s);
                    s = strtok_r(NULL, ",", &lasts);
                }
            }

            if (obj->element)
            {
                CONFIG_PARAMETER *params = obj->parameters;
                while (params)
                {
                    if (strcmp(params->name, "module") && strcmp(params->name, "options"))
                    {
                        filterAddParameter(obj->element,
                                           params->name,
                                           params->value);
                    }
                    params = params->next;
                }
            }
        }
        obj = obj->next;
    }

    /*
     * Now we have the services we can add the servers to the services
     * add the protocols to the services
     */
    obj = context;
    while (obj)
    {
        char *type = config_get_value(obj->parameters, "type");
        if (type == NULL)
        {
            ;
        }
        else if (!strcmp(type, "service"))
        {
            char *servers;
            char *roptions;
            char *router;
            char *filters = config_get_value(obj->parameters, "filters");
            servers = config_get_value(obj->parameters, "servers");
            roptions = config_get_value(obj->parameters, "router_options");
            router = config_get_value(obj->parameters, "router");
            if (servers && obj->element)
            {
                char *lasts;
                char *s = strtok_r(servers, ",", &lasts);
                while (s)
                {
                    CONFIG_CONTEXT *obj1 = context;
                    int found = 0;
                    while (obj1)
                    {
                        if (strcmp(trim(s), obj1->object) == 0 && obj->element && obj1->element)
                        {
                            found = 1;
                            serviceAddBackend(obj->element, obj1->element);
                        }
                        obj1 = obj1->next;
                    }

                    if (!found)
                    {
                        MXS_ERROR("Unable to find "
                                  "server '%s' that is "
                                  "configured as part of "
                                  "service '%s'.",
                                  s, obj->object);
                    }
                    s = strtok_r(NULL, ",", &lasts);
                }
            }
            else if (servers == NULL && !isInternalService(router))
            {
                MXS_WARNING("The service '%s' is missing a "
                            "definition of the servers that provide "
                            "the service.",
                            obj->object);
            }

            if (roptions && obj->element)
            {
                char *lasts;
                char *s = strtok_r(roptions, ",", &lasts);
                while (s)
                {
                    serviceAddRouterOption(obj->element, s);
                    s = strtok_r(NULL, ",", &lasts);
                }
            }

            if (filters && obj->element)
            {
                if (!serviceSetFilters(obj->element, filters))
                {
                    error_count++;
                }
            }
        }
        else if (!strcmp(type, "listener"))
        {
            char *service;
            char *address;
            char *port;
            char *protocol;
            char *socket;
            struct sockaddr_in serv_addr;

            service = config_get_value(obj->parameters, "service");
            port = config_get_value(obj->parameters, "port");
            address = config_get_value(obj->parameters, "address");
            protocol = config_get_value(obj->parameters, "protocol");
            socket = config_get_value(obj->parameters, "socket");

            /* if id is not set, do it now */
            if (gateway.id == 0)
            {
                setipaddress(&serv_addr.sin_addr, (address == NULL) ? "0.0.0.0" : address);
                gateway.id = (unsigned long) (serv_addr.sin_addr.s_addr +
                                              (port != NULL ? atoi(port) : 0 + getpid()));
            }

            if (service && protocol && (socket || port))
            {
                if (socket)
                {
                    CONFIG_CONTEXT *ptr = context;
                    while (ptr && strcmp(ptr->object, service) != 0)
                    {
                        ptr = ptr->next;
                    }

                    if (ptr && ptr->element)
                    {
                        serviceAddProtocol(ptr->element, protocol, socket, 0);
                    }
                    else
                    {
                        MXS_ERROR("Listener '%s', "
                                  "service '%s' not found. "
                                  "Listener will not execute for socket %s.",
                                  obj->object, service, socket);
                        error_count++;
                    }
                }

                if (port)
                {
                    CONFIG_CONTEXT *ptr = context;
                    while (ptr && strcmp(ptr->object, service) != 0)
                    {
                        ptr = ptr->next;
                    }

                    if (ptr && ptr->element)
                    {
                        serviceAddProtocol(ptr->element, protocol, address, atoi(port));
                    }
                    else
                    {
                        MXS_ERROR("Listener '%s', "
                                  "service '%s' not found. "
                                  "Listener will not execute.",
                                  obj->object, service);
                        error_count++;
                    }
                }
            }
            else
            {
                MXS_ERROR("Listener '%s' is missing a "
                          "required "
                          "parameter. A Listener must have a "
                          "service, port and protocol defined.",
                          obj->object);
                error_count++;
            }
        }
        else if (!strcmp(type, "monitor"))
        {
            char *module;
            char *servers;
            char *user;
            char *passwd;
            unsigned long interval = 0;
            int connect_timeout = 0;
            int read_timeout = 0;
            int write_timeout = 0;

            module = config_get_value(obj->parameters, "module");
            servers = config_get_value(obj->parameters, "servers");
            user = config_get_value(obj->parameters, "user");
            passwd = config_get_value(obj->parameters, "passwd");
            if (config_get_value(obj->parameters, "monitor_interval"))
            {
                interval = strtoul(config_get_value(obj->parameters, "monitor_interval"), NULL, 10);
            }

            if (config_get_value(obj->parameters, "backend_connect_timeout"))
            {
                connect_timeout = atoi(config_get_value(obj->parameters, "backend_connect_timeout"));
            }
            if (config_get_value(obj->parameters, "backend_read_timeout"))
            {
                read_timeout = atoi(config_get_value(obj->parameters, "backend_read_timeout"));
            }
            if (config_get_value(obj->parameters, "backend_write_timeout"))
            {
                write_timeout = atoi(config_get_value(obj->parameters, "backend_write_timeout"));
            }

            if (module)
            {
                obj->element = monitor_alloc(obj->object, module);
                if (servers && obj->element)
                {
                    char *s, *lasts;

                    /* if id is not set, compute it now with pid only */
                    if (gateway.id == 0)
                    {
                        gateway.id = getpid();
                    }

                    monitorAddParameters(obj->element, obj->parameters);

                    /* set monitor interval */
                    if (interval > 0)
                    {
                        monitorSetInterval(obj->element, interval);
                    }
                    else
                    {
                        MXS_WARNING("Monitor '%s' "
                                    "missing monitor_interval parameter, "
                                    "default value of 10000 miliseconds.", obj->object);
                    }

                    /* set timeouts */
                    if (connect_timeout > 0)
                    {
                        monitorSetNetworkTimeout(obj->element, MONITOR_CONNECT_TIMEOUT, connect_timeout);
                    }
                    if (read_timeout > 0)
                    {
                        monitorSetNetworkTimeout(obj->element, MONITOR_READ_TIMEOUT, read_timeout);
                    }
                    if (write_timeout > 0)
                    {
                        monitorSetNetworkTimeout(obj->element, MONITOR_WRITE_TIMEOUT, write_timeout);
                    }

                    /* get the servers to monitor */
                    s = strtok_r(servers, ",", &lasts);
                    while (s)
                    {
                        CONFIG_CONTEXT *obj1 = context;
                        int             found = 0;
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
                            MXS_ERROR("Unable to find "
                                      "server '%s' that is "
                                      "configured in the "
                                      "monitor '%s'.",
                                      s, obj->object);
                        }

                        s = strtok_r(NULL, ",", &lasts);
                    }
                }

                if (obj->element && user && passwd)
                {
                    monitorAddUser(obj->element, user, passwd);
                    check_monitor_permissions(obj->element);
                }
                else if (obj->element && user)
                {
                    MXS_ERROR("Monitor '%s' defines a "
                              "username with no password.",
                              obj->object);
                    error_count++;
                }
            }
            else
            {
                obj->element = NULL;
                MXS_ERROR("Monitor '%s' is missing a "
                          "require module parameter.",
                          obj->object);
                error_count++;
            }
        }
        else if (strcmp(type, "server") != 0 && strcmp(type, "filter") != 0)
        {
            MXS_ERROR("Configuration object '%s' has an "
                      "invalid type specified.",
                      obj->object);
            error_count++;
        }

        obj = obj->next;
    } /*< while */

    /** TODO: consistency check function */

    hashtable_free(monitorhash);
    /**
     * error_count += consistency_checks();
     */

    if (error_count)
    {
        MXS_ERROR("%d errors where encountered processing the "
                  "configuration file '%s'.",
                  error_count,
                  config_file);
        return 0;
    }

    return 1;
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
            switch (ptype) {
            case COUNT_TYPE:
                *val = param->qfd.valcount;
                succp = true;
                goto return_succp;

            case PERCENT_TYPE:
                *val = param->qfd.valpercent;
                succp  =true;
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

    p2 = (CONFIG_PARAMETER*) malloc(sizeof(CONFIG_PARAMETER));

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
        free(p1->name);
        free(p1->value);
        CONFIG_PARAMETER* p2 = p1->next;
        free(p1);
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
        free(context->object);
        free_config_parameter(context->parameters);
        obj = context->next;
        free(context);
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
} lognames[] = {
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
        int thrcount = atoi(value);
        if (thrcount > 0)
        {
            gateway.n_threads = thrcount;
        }
        else
        {
            MXS_WARNING("Invalid value for 'threads': %s.", value);
            return 0;
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
        feedback.feedback_user_info = strdup(value);
    }
    else if (strcmp(name, "feedback_url") == 0)
    {
        feedback.feedback_url = strdup(value);
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
    uint8_t mac_addr[6]="";
    struct utsname uname_data;
    gateway.n_threads = get_processor_count();
    gateway.n_nbpoll = DEFAULT_NBPOLLS;
    gateway.pollsleep = DEFAULT_POLLSLEEP;
    gateway.auth_conn_timeout = DEFAULT_AUTH_CONNECT_TIMEOUT;
    gateway.auth_read_timeout = DEFAULT_AUTH_READ_TIMEOUT;
    gateway.auth_write_timeout = DEFAULT_AUTH_WRITE_TIMEOUT;
    if (version_string != NULL)
    {
        gateway.version_string = strdup(version_string);
    }
    else
    {
        gateway.version_string = NULL;
    }
    gateway.id=0;

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
        strncpy(gateway.sysname, uname_data.sysname, _SYSNAME_STR_LENGTH);
    }
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
            char *router = config_get_value(obj->parameters,
                                            "router");
            if (router)
            {
                if ((service = service_find(obj->object)) != NULL)
                {
                    char *user;
                    char *auth;
                    char *enable_root_user;

                    char *connection_timeout;

                    char* auth_all_servers;
                    char* optimize_wildcard;
                    char* strip_db_esc;
                    char* max_slave_conn_str;
                    char* max_slave_rlag_str;
                    char *version_string;
                    char *allow_localhost_match_wildcard_host;

                    enable_root_user = config_get_value(obj->parameters, "enable_root_user");

                    connection_timeout = config_get_value(obj->parameters, "connection_timeout");
                    user = config_get_value(obj->parameters, "user");
                    auth = config_get_value(obj->parameters, "passwd");

                    auth_all_servers = config_get_value(obj->parameters, "auth_all_servers");
                    optimize_wildcard = config_get_value(obj->parameters, "optimize_wildcard");
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
                            free(service->version_string);
                        }
                        service->version_string = strdup(version_string);
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

                        if (auth_all_servers)
                        {
                            serviceAuthAllServers(service, config_truth_value(auth_all_servers));
                        }
                        if (optimize_wildcard)
                        {
                            serviceOptimizeWildcard(service, config_truth_value(optimize_wildcard));
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
                                succp = service_set_param_value(
                                    service,
                                    param,
                                    max_slave_conn_str,
                                    COUNT_ATMOST,
                                    (PERCENT_TYPE|COUNT_TYPE));
                            }

                            if (!succp && param != NULL)
                            {
                                MXS_WARNING("Invalid value type "
                                            "for parameter \'%s.%s = %s\'\n\tExpected "
                                            "type is either <int> for slave connection "
                                            "count or\n\t<int>%% for specifying the "
                                            "maximum percentage of available the "
                                            "slaves that will be connected.",
                                            ((SERVICE*)obj->element)->name,
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
                                                ((SERVICE*)obj->element)->name,
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
                    char *user;
                    char *auth;
                    char *enable_root_user;
                    char *connection_timeout;
                    char *allow_localhost_match_wildcard_host;

                    enable_root_user = config_get_value(obj->parameters,
                                                        "enable_root_user");

                    connection_timeout = config_get_value(obj->parameters,
                                                          "connection_timeout");

                    allow_localhost_match_wildcard_host =
                        config_get_value(obj->parameters, "localhost_match_wildcard_host");

                    user = config_get_value(obj->parameters, "user");
                    auth = config_get_value(obj->parameters, "passwd");
                    obj->element = service_alloc(obj->object, router);

                    if (obj->element && user && auth)
                    {
                        serviceSetUser(obj->element,
                                       user,
                                       auth);
                        if (enable_root_user)
                        {
                            serviceEnableRootUser(obj->element, config_truth_value(enable_root_user));
                        }

                        if (connection_timeout)
                        {
                            serviceSetTimeout(obj->element, atoi(connection_timeout));
                        }

                        if (allow_localhost_match_wildcard_host)
                        {
                            serviceEnableLocalhostMatchWildcardHost(
                                obj->element,
                                config_truth_value(allow_localhost_match_wildcard_host));
                        }
                    }
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
            char *address;
            char *port;
            char *protocol;
            char *monuser;
            char *monpw;

            address = config_get_value(obj->parameters, "address");
            port = config_get_value(obj->parameters, "port");
            protocol = config_get_value(obj->parameters, "protocol");
            monuser = config_get_value(obj->parameters, "monitoruser");
            monpw = config_get_value(obj->parameters, "monitorpw");

            if (address && port && protocol)
            {
                if ((server = server_find(address, atoi(port))) != NULL)
                {
                    server_update(server,
                                  protocol,
                                  monuser,
                                  monpw);
                    obj->element = server;
                }
                else
                {
                    obj->element = server_alloc(address,
                                                protocol,
                                                atoi(port));

                    server_set_unique_name(obj->element, obj->object);

                    if (obj->element && monuser && monpw)
                    {
                        serverAddMonUser(obj->element,
                                         monuser,
                                         monpw);
                    }
                }
            }
            else
            {
                MXS_ERROR("Server '%s' is missing a "
                          "required "
                          "configuration parameter. A server must "
                          "have address, port and protocol "
                          "defined.",
                          obj->object);
            }
        }
        obj = obj->next;
    }

    /*
     * Now we have the services we can add the servers to the services
     * add the protocols to the services
     */
    obj = context;
    while (obj)
    {
        char *type = config_get_value(obj->parameters, "type");
        if (type == NULL)
        {
            ;
        }
        else if (!strcmp(type, "service"))
        {
            char *servers;
            char *roptions;
            char *filters;

            servers = config_get_value(obj->parameters, "servers");
            roptions = config_get_value(obj->parameters, "router_options");
            filters = config_get_value(obj->parameters, "filters");

            if (servers && obj->element)
            {
                char *lasts;
                char *s = strtok_r(servers, ",", &lasts);
                while (s)
                {
                    CONFIG_CONTEXT *obj1 = context;
                    int             found = 0;
                    while (obj1)
                    {
                        if (strcmp(trim(s), obj1->object) == 0 && obj->element && obj1->element)
                        {
                            found = 1;
                            if (!serviceHasBackend(obj->element, obj1->element))
                            {
                                serviceAddBackend(obj->element, obj1->element);
                            }
                        }

                        obj1 = obj1->next;
                    }
                    if (!found)
                    {
                        MXS_ERROR("Unable to find "
                                  "server '%s' that is "
                                  "configured as part of "
                                  "service '%s'.",
                                  s, obj->object);
                    }
                    s = strtok_r(NULL, ",", &lasts);
                }
            }
            if (roptions && obj->element)
            {
                char *lasts;
                char *s = strtok_r(roptions, ",", &lasts);
                serviceClearRouterOptions(obj->element);
                while (s)
                {
                    serviceAddRouterOption(obj->element, s);
                    s = strtok_r(NULL, ",", &lasts);
                }
            }
            if (filters && obj->element)
            {
                if (!serviceSetFilters(obj->element, filters))
                {
                    MXS_ERROR("Failed to set service filters for '%s'. This "
                              "service will not use filters.", obj->object);
                }
            }
        }
        else if (!strcmp(type, "listener"))
        {
            char *service;
            char *port;
            char *protocol;
            char *address;
            char *socket;

            service = config_get_value(obj->parameters, "service");
            address = config_get_value(obj->parameters, "address");
            port = config_get_value(obj->parameters, "port");
            protocol = config_get_value(obj->parameters, "protocol");
            socket = config_get_value(obj->parameters, "socket");

            if (service && socket && protocol)
            {
                CONFIG_CONTEXT *ptr = context;
                while (ptr && strcmp(ptr->object, service) != 0)
                {
                    ptr = ptr->next;
                }

                if (ptr &&
                    ptr->element &&
                    serviceHasProtocol(ptr->element, protocol, 0) == 0)
                {
                    serviceAddProtocol(ptr->element, protocol, socket, 0);
                    serviceStartProtocol(ptr->element, protocol, 0);
                }
            }

            if (service && port && protocol)
            {
                CONFIG_CONTEXT *ptr = context;

                while (ptr && strcmp(ptr->object, service) != 0)
                {
                    ptr = ptr->next;
                }

                if (ptr &&
                    ptr->element &&
                    serviceHasProtocol(ptr->element, protocol, atoi(port)) == 0)
                {
                    serviceAddProtocol(ptr->element, protocol, address, atoi(port));
                    serviceStartProtocol(ptr->element, protocol, atoi(port));
                }
            }
        }
        else if (strcmp(type, "server") != 0 &&
                 strcmp(type, "monitor") != 0 &&
                 strcmp(type, "filter") != 0)
        {
            MXS_ERROR("Configuration object %s has an invalid type specified.", obj->object);
        }
        obj = obj->next;
    }

    return 1;
}

static char *service_params[] =
{
    "type",
    "router",
    "router_options",
    "servers",
    "user",
    "passwd",
    "enable_root_user",
    "connection_timeout",
    "auth_all_servers",
    "optimize_wildcard",
    "strip_db_esc",
    "localhost_match_wildcard_host",
    "max_slave_connections",
    "max_slave_replication_lag",
    "use_sql_variables_in",         /*< rwsplit only */
    "subservices",
    "version_string",
    "filters",
    "weightby",
    "ssl_cert",
    "ssl_ca_cert",
    "ssl",
    "ssl_key",
    "ssl_version",
    "ssl_cert_verify_depth",
    "ignore_databases",
    "ignore_databases_regex",
    "log_auth_warnings",
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
    NULL
};

static char *monitor_params[] =
{
    "type",
    "module",
    "servers",
    "user",
    "passwd",
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
    NULL
};
/**
 * Check the configuration objects have valid parameters
 */
static void
check_config_objects(CONFIG_CONTEXT *context)
{
    CONFIG_CONTEXT   *obj;
    CONFIG_PARAMETER *params;
    char             *type, **param_set;
    int              i;

    /**
     * Process the data and create the services and servers defined
     * in the data.
     */
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
                for (i = 0; param_set[i]; i++)
                {
                    if (!strcmp(params->name, param_set[i]))
                    {
                        found = 1;
                    }
                }

                if (found == 0)
                {
                    MXS_ERROR("Unexpected parameter "
                              "'%s' for object '%s' of type "
                              "'%s'.",
                              params->name,
                              obj->object,
                              type);
                }
                params = params->next;
            }
        }
        obj = obj->next;
    }
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
        strcasecmp(str, "no") == 0|| strcasecmp(str, "0") == 0)
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
    NULL
};

/**
 * Determine if the router is one of the special internal services that
 * MaxScale offers.
 *
 * @param router        The router name
 * @return      Non-zero if the router is in the InternalRouters table
 */
bool
isInternalService(char *router)
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
            { /* don't count loopback */
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

    return success;
}

/**
 * Get the linux distribution info
 *
 * @param release       The allocated buffer where
 *                      the found distribution is copied into.
 * @return 1 on success, 0 on failure
 *
 */
int
config_get_release_string(char* release)
{
    const char *masks[] =
        {
            "/etc/*-version", "/etc/*-release",
            "/etc/*_version", "/etc/*_release"
        };

    bool have_distribution;
    char distribution[_RELEASE_STR_LENGTH]="";
    int fd;
    int i;
    char *to;

    have_distribution= false;

    /* get data from lsb-release first */
    if ((fd = open("/etc/lsb-release", O_RDONLY)) != -1)
    {
        /* LSB-compliant distribution! */
        size_t len = read(fd, (char*)distribution, sizeof(distribution) - 1);
        close(fd);

        if (len != (size_t)-1)
        {
            distribution[len]= 0;

            char *found= strstr(distribution, "DISTRIB_DESCRIPTION=");

            if (found)
            {
                have_distribution = true;
                char *end = strstr(found, "\n");
                if (end == NULL)
                {
                    end = distribution + len;
                }
                found += 20;

                if (*found == '"' && end[-1] == '"')
                {
                    found++;
                    end--;
                }
                *end = 0;

                to = strcpy(distribution, "lsb: ");
                memmove(to, found, end - found + 1 < INT_MAX ? end - found + 1 : INT_MAX);

                strncpy(release, to, _RELEASE_STR_LENGTH);

                return 1;
            }
        }
    }

    /* if not an LSB-compliant distribution */
    for (i = 0; !have_distribution && i < 4; i++)
    {
        glob_t found;
        char *new_to;

        if (glob(masks[i], GLOB_NOSORT, NULL, &found) == 0)
        {
            int fd;
            int k = 0;
            int skipindex = 0;
            int startindex = 0;

            for (k = 0; k< found.gl_pathc; k++)
            {
                if (strcmp(found.gl_pathv[k], "/etc/lsb-release") == 0)
                {
                    skipindex = k;
                }
            }

            if (skipindex == 0)
                startindex++;

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

                if (len != (size_t)-1)
                {
                    new_to[len]= 0;
                    char *end= strstr(new_to, "\n");
                    if (end)
                    {
                        *end= 0;
                    }

                    have_distribution= true;
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
    CONFIG_PARAMETER* nptr = malloc(sizeof(CONFIG_PARAMETER));

    if (nptr == NULL)
    {
        MXS_ERROR("Memory allocation failed when adding configuration parameters.");
        return;
    }

    nptr->name = strdup(key);
    nptr->value = strdup(value);
    nptr->next = obj->parameters;
    obj->parameters = nptr;
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
    HASHTABLE *hash = hashtable_alloc(table_size, simple_str_hash, strcmp);
    pcre2_code *re = pcre2_compile((PCRE2_SPTR) "^\\s*\\[(.+)\\]\\s*$", PCRE2_ZERO_TERMINATED,
                                   0, &errcode, &erroffset, NULL);
    pcre2_match_data *mdata = NULL;
    int size = 1024;
    char *buffer = malloc(size * sizeof(char));

    if (buffer && hash && re && (mdata = pcre2_match_data_create_from_pattern(re, NULL)))
    {
        hashtable_memory_fns(hash, (HASHMEMORYFN) strdup, NULL,
                             (HASHMEMORYFN) free, NULL);
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
        MXS_ERROR("Failed to allocate enough memory when checking"
                  " for duplicate sections in configuration file.");
        rval = true;
    }

    hashtable_free(hash);
    pcre2_code_free(re);
    pcre2_match_data_free(mdata);
    free(buffer);
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
            char* tmp = (char*) realloc(destptr, *size * 2);
            if (tmp)
            {
                destptr = tmp;
                *size *= 2;
            }
            else
            {
                MXS_ERROR("Failed to reallocate memory from %d"
                          " bytes to %d bytes when reading from file.",
                          *size, *size * 2);
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
