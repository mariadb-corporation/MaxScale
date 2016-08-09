/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file cdc_auth.c
 *
 * CDC Authentication module for handling the checking of clients credentials
 * in the CDC protocol.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 11/03/2016   Massimiliano Pinto      Initial version
 *
 * @endverbatim
 */

#include <gw_authenticator.h>
#include <cdc.h>
#include <modutil.h>
#include <users.h>
#include <sys/stat.h>

/* Allowed time interval (in seconds) after last update*/
#define CDC_USERS_REFRESH_TIME 30
/* Max number of load calls within the time interval */
#define CDC_USERS_REFRESH_MAX_PER_TIME 4


MODULE_INFO info =
{
    MODULE_API_AUTHENTICATOR,
    MODULE_GA,
    GWAUTHENTICATOR_VERSION,
    "The CDC client to MaxScale authenticator implementation"
};

static char *version_str = "V1.0.0";
static int   cdc_load_users_init = 0;

static int  cdc_auth_set_protocol_data(DCB *dcb, GWBUF *buf);
static bool cdc_auth_is_client_ssl_capable(DCB *dcb);
static int  cdc_auth_authenticate(DCB *dcb);
static void cdc_auth_free_client_data(DCB *dcb);

static int cdc_set_service_user(SERVICE *service);
static int cdc_read_users(USERS *users, char *usersfile);
static int cdc_load_users(SERVICE *service);
static int cdc_refresh_users(SERVICE *service);
static int cdc_replace_users(SERVICE *service);

extern char  *gw_bin2hex(char *out, const uint8_t *in, unsigned int len);
extern void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out);
extern char *create_hex_sha1_sha1_passwd(char *passwd);
extern char *decryptPassword(char *crypt);


/*
 * The "module object" for mysql client authenticator module.
 */
static GWAUTHENTICATOR MyObject =
{
    cdc_auth_set_protocol_data,           /* Extract data into structure   */
    cdc_auth_is_client_ssl_capable,       /* Check if client supports SSL  */
    cdc_auth_authenticate,                /* Authenticate user credentials */
    cdc_auth_free_client_data,            /* Free the client data held in DCB */
};

static int cdc_auth_check(
    DCB           *dcb,
    CDC_protocol  *protocol,
    char          *username,
    uint8_t       *auth_data,
    unsigned int  *flags
);

static int cdc_auth_set_client_data(
    CDC_session *client_data,
    CDC_protocol *protocol,
    uint8_t *client_auth_packet,
    int client_auth_packet_size
);

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char* version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void ModuleInit()
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
GWAUTHENTICATOR* GetModuleObject()
{
    return &MyObject;
}

/**
 * @brief Function to easily call authentication check.
 *
 * @param dcb Request handler DCB connected to the client
 * @param protocol  The protocol structure for the connection
 * @param username  String containing username
 * @param auth_data  The encrypted password for authentication
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
static int cdc_auth_check(DCB *dcb, CDC_protocol *protocol, char *username, uint8_t *auth_data,
                          unsigned int *flags)
{
    char *user_password;

    if (!cdc_load_users_init)
    {
        /* Load db users or set service user */
        if (cdc_load_users(dcb->service) < 1)
        {
            cdc_set_service_user(dcb->service);
        }

        cdc_load_users_init = 1;
    }

    user_password = users_fetch(dcb->service->users, username);

    if (!user_password)
    {
        return CDC_STATE_AUTH_FAILED;
    }
    else
    {
        /* compute SHA1 of auth_data */
        uint8_t sha1_step1[SHA_DIGEST_LENGTH] = "";
        char hex_step1[2 * SHA_DIGEST_LENGTH + 1] = "";

        gw_sha1_str(auth_data, SHA_DIGEST_LENGTH, sha1_step1);
        gw_bin2hex(hex_step1, sha1_step1, SHA_DIGEST_LENGTH);

        if (memcmp(user_password, hex_step1, SHA_DIGEST_LENGTH) == 0)
        {
            return CDC_STATE_AUTH_OK;
        }
        else
        {
            return CDC_STATE_AUTH_FAILED;
        }
    }
}

/**
 * @brief Authenticates a CDC user who is a client to MaxScale.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
static int
cdc_auth_authenticate(DCB *dcb)
{
    CDC_protocol *protocol = DCB_PROTOCOL(dcb, CDC_protocol);
    CDC_session *client_data = (CDC_session *)dcb->data;
    int auth_ret;

    if (0 == strlen(client_data->user))
    {
        auth_ret = CDC_STATE_AUTH_ERR;
    }
    else
    {
        MXS_DEBUG("Receiving connection from '%s'",
                  client_data->user);

        auth_ret = cdc_auth_check(dcb, protocol, client_data->user, client_data->auth_data, client_data->flags);

        /* On failed authentication try to reload user table */
        if (CDC_STATE_AUTH_OK != auth_ret && 0 == cdc_refresh_users(dcb->service))
        {
            /* Call protocol authentication */
            auth_ret = cdc_auth_check(dcb, protocol, client_data->user, client_data->auth_data, client_data->flags);
        }

        /* on successful authentication, set user into dcb field */
        if (CDC_STATE_AUTH_OK == auth_ret)
        {
            dcb->user = strdup(client_data->user);
        }
        else if (dcb->service->log_auth_warnings)
        {
            MXS_NOTICE("%s: login attempt for user '%s', authentication failed.",
                       dcb->service->name, client_data->user);
        }
    }

    return auth_ret;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * The request handler DCB has a field called data that contains protocol
 * specific information. This function examines a buffer containing CDC
 * authentication data and puts it into a structure that is referred to
 * by the DCB. If the information in the buffer is invalid, then a failure
 * code is returned. A call to cdc_auth_set_client_data does the
 * detailed work.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffer containing data from client
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
static int
cdc_auth_set_protocol_data(DCB *dcb, GWBUF *buf)
{
    uint8_t *client_auth_packet = GWBUF_DATA(buf);
    CDC_protocol *protocol = NULL;
    CDC_session *client_data = NULL;
    int client_auth_packet_size = 0;

    protocol = DCB_PROTOCOL(dcb, CDC_protocol);
    CHK_PROTOCOL(protocol);
    if (dcb->data == NULL)
    {
        if (NULL == (client_data = (CDC_session *)calloc(1, sizeof(CDC_session))))
        {
            return CDC_STATE_AUTH_ERR;
        }
        dcb->data = client_data;
    }
    else
    {
        client_data = (CDC_session *)dcb->data;
    }

    client_auth_packet_size = gwbuf_length(buf);

    return cdc_auth_set_client_data(client_data, protocol, client_auth_packet,
                                    client_auth_packet_size);
}

/**
 * @brief Transfer detailed data from the authentication request to the DCB.
 *
 * The caller has created the data structure pointed to by the DCB, and this
 * function fills in the details. If problems are found with the data, the
 * return code indicates failure.
 *
 * @param client_data The data structure for the DCB
 * @param protocol The protocol structure for this connection
 * @param client_auth_packet The data from the buffer received from client
 * @param client_auth_packet size An integer giving the size of the data
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
static int
cdc_auth_set_client_data(CDC_session *client_data,
                         CDC_protocol *protocol,
                         uint8_t *client_auth_packet,
                         int client_auth_packet_size)
{
    int rval = CDC_STATE_AUTH_ERR;
    int decoded_size = client_auth_packet_size / 2;
    char decoded_buffer[decoded_size + 1]; // Extra for terminating null

    /* decode input data */
    if (client_auth_packet_size <= CDC_USER_MAXLEN)
    {
        gw_hex2bin((uint8_t*)decoded_buffer, (const char *)client_auth_packet,
                   client_auth_packet_size);
        decoded_buffer[decoded_size] = '\0';
        char *tmp_ptr = strchr(decoded_buffer, ':');

        if (tmp_ptr)
        {
            size_t user_len = tmp_ptr - decoded_buffer;
            *tmp_ptr++ = '\0';
            size_t auth_len = decoded_size - (tmp_ptr - decoded_buffer);

            if (user_len <= CDC_USER_MAXLEN && auth_len == SHA_DIGEST_LENGTH)
            {
                strcpy(client_data->user, decoded_buffer);
                memcpy(client_data->auth_data, tmp_ptr, auth_len);
                rval = CDC_STATE_AUTH_OK;
            }
        }
        else
        {
            MXS_ERROR("Authentication failed, the decoded client authentication "
                      "packet is malformed. Expected <username>:SHA1(<password>)");
        }
    }
    else
    {
        MXS_ERROR("Authentication failed, client authentication packet length "
                  "exceeds the maximum allowed length of %d bytes.", CDC_USER_MAXLEN);
    }

    return rval;
}

/**
 * @brief Determine whether the client is SSL capable
 *
 * The authentication request from the client will indicate whether the client
 * is expecting to make an SSL connection. The information has been extracted
 * in the previous functions.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether client is SSL capable
 */
static bool
cdc_auth_is_client_ssl_capable(DCB *dcb)
{
    return false;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * Currently all that is required is to free the storage pointed to by
 * dcb->data.  But this is intended to be implemented as part of the
 * authentication API at which time this code will be moved into the
 * CDC authenticator.  If the data structure were to become more complex
 * the mechanism would still work and be the responsibility of the authenticator.
 * The DCB should not know authenticator implementation details.
 *
 * @param dcb Request handler DCB connected to the client
 */
static void
cdc_auth_free_client_data(DCB *dcb)
{
    free(dcb->data);
}

/*
 * Add the service user to CDC dbusers (service->users)
 * via cdc_users_alloc
 *
 * @param service   The current service
 * @return      0 on success, 1 on failure
 */
static int
cdc_set_service_user(SERVICE *service)
{
    char *dpwd = NULL;
    char *newpasswd = NULL;
    char *service_user = NULL;
    char *service_passwd = NULL;

    if (serviceGetUser(service, &service_user, &service_passwd) == 0)
    {
        MXS_ERROR("failed to get service user details for service %s",
                  service->name);

        return 1;
    }

    dpwd = decryptPassword(service->credentials.authdata);

    if (!dpwd)
    {
        MXS_ERROR("decrypt password failed for service user %s, service %s",
                  service_user,
                  service->name);

        return 1;
    }

    newpasswd = create_hex_sha1_sha1_passwd(dpwd);

    if (!newpasswd)
    {
        MXS_ERROR("create hex_sha1_sha1_password failed for service user %s",
                  service_user);

        free(dpwd);
        return 1;
    }

    /* add service user */
    (void)users_add(service->users, service->credentials.name, newpasswd);

    free(newpasswd);
    free(dpwd);

    return 0;
}

/*
 * Load AVRO users into (service->users)
 *
 * @param service    The current service
 * @return          -1 on failure, 0 for no users found, > 0 for found users
 */
static int
cdc_load_users(SERVICE *service)
{
    int loaded = -1;
    char path[PATH_MAX + 1] = "";

    /* File path for router cached authentication data */
    snprintf(path, PATH_MAX, "%s/%s/cdcusers", get_datadir(), service->name);

    /* Allocate users table */
    if (service->users == NULL)
    {
        service->users = users_alloc();
    }

    /* Try loading authentication data from file cache */
    loaded = cdc_read_users(service->users, path);

    if (loaded == -1)
    {
        MXS_ERROR("Service %s, Unable to read AVRO users information from %s."
                  " No AVRO user added to service users table. Service user is still allowed to connect.",
                  service->name,
                  path);
    }

    /* At service start last update is set to CDC_USERS_REFRESH_TIME seconds
     * earlier. This way MaxScale could try reloading users just after startup.
     */
    service->rate_limit.last = time(NULL) - CDC_USERS_REFRESH_TIME;
    service->rate_limit.nloads = 1;

    return loaded;
}

/**
 * Load the AVRO users
 *
 * @param service    Current service
 * @param usersfile  File with users
 * @return -1 on error or users loaded (including 0)
 */

static int
cdc_read_users(USERS *users, char *usersfile)
{
    FILE  *fp;
    int loaded = 0;
    char *avro_user;
    char *user_passwd;
    /* user maxlen ':' password hash  '\n' '\0' */
    char read_buffer[CDC_USER_MAXLEN + 1 + SHA_DIGEST_LENGTH + 1 + 1];
    char *all_users_data = NULL;
    struct  stat    statb;
    int fd;
    int filelen = 0;
    unsigned char hash[SHA_DIGEST_LENGTH] = "";

    int max_line_size = sizeof(read_buffer) - 1;

    if ((fp = fopen(usersfile, "r")) == NULL)
    {
        return -1;
    }

    fd = fileno(fp);

    if (fstat(fd, &statb) == 0)
    {
        filelen = statb.st_size;
    }

    if ((all_users_data = malloc(filelen + 1)) == NULL)
    {
        MXS_ERROR("failed to allocate %i for service user data load %s",
                  filelen + 1,
                  usersfile);
        return -1;
    }

    *all_users_data = '\0';

    while (!feof(fp))
    {
        if (fgets(read_buffer, max_line_size, fp) != NULL)
        {
            char *tmp_ptr = read_buffer;
            /* append data for hash */
            strcat(all_users_data, read_buffer);

            if ((tmp_ptr = strchr(read_buffer, ':')) != NULL)
            {
                *tmp_ptr++ = '\0';
                avro_user = read_buffer;
                user_passwd = tmp_ptr;
                if ((tmp_ptr = strchr(user_passwd, '\n')) != NULL)
                {
                    *tmp_ptr = '\0';
                }

                /* add user */
                users_add(users, avro_user, user_passwd);

                loaded++;
            }
        }
    }

    /* compute SHA1 digest for users' data */
    SHA1((const unsigned char *) all_users_data, strlen(all_users_data), hash);

    memcpy(users->cksum, hash, SHA_DIGEST_LENGTH);

    free(all_users_data);

    fclose(fp);

    return loaded;
}


/**
 *  * Refresh the database users for the service
 *   * This function replaces the MySQL users used by the service with the latest
 *    * version found on the backend servers. There is a limit on how often the users
 *     * can be reloaded and if this limit is exceeded, the reload will fail.
 *      * @param service Service to reload
 *       * @return 0 on success and 1 on error
 *        */
static int
cdc_refresh_users(SERVICE *service)
{
    int ret = 1;
    /* check for another running getUsers request */
    if (!spinlock_acquire_nowait(&service->users_table_spin))
    {
        MXS_DEBUG("%s: [service_refresh_users] failed to get get lock for "
                  "loading new users' table: another thread is loading users",
                  service->name);

        return 1;
    }

    /* check if refresh rate limit has exceeded */
    if ((time(NULL) < (service->rate_limit.last + CDC_USERS_REFRESH_TIME)) ||
        (service->rate_limit.nloads > CDC_USERS_REFRESH_MAX_PER_TIME))
    {
        spinlock_release(&service->users_table_spin);
        MXS_ERROR("%s: Refresh rate limit exceeded for load of users' table.",
                  service->name);

        return 1;
    }

    service->rate_limit.nloads++;

    /* update time and counter */
    if (service->rate_limit.nloads > CDC_USERS_REFRESH_MAX_PER_TIME)
    {
        service->rate_limit.nloads = 1;
        service->rate_limit.last = time(NULL);
    }

    ret = cdc_replace_users(service);

    /* remove lock */
    spinlock_release(&service->users_table_spin);

    if (ret >= 0)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/* Replace the user/passwd in the servicei users tbale from a db file.
 * The replacement is succesful only if the users' table checksums differ
 *
 * @param service   The current service
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 *      */
static int
cdc_replace_users(SERVICE *service)
{
    int i;
    USERS *newusers, *oldusers;
    HASHTABLE *oldresources;
    char path[PATH_MAX + 1] = "";

    /* File path for router cached authentication data */
    snprintf(path, PATH_MAX, "%s/%s/cdcusers", get_datadir(), service->name);

    if ((newusers = users_alloc()) == NULL)
    {
        return -1;
    }


    /* load users */
    i = cdc_read_users(newusers, path);

    if (i <= 0)
    {
        users_free(newusers);
        return i;
    }

    spinlock_acquire(&service->spin);
    oldusers = service->users;

    /* digest compare */
    if (oldusers != NULL && memcmp(oldusers->cksum, newusers->cksum,
                                   SHA_DIGEST_LENGTH) == 0)
    {
        /* same data, nothing to do */
        MXS_DEBUG("%lu [cdc_replace_users] users' tables not switched, checksum is the same",
                  pthread_self());

        /* free the new table */
        users_free(newusers);
        i = 0;
    }
    else
    {
        /* replace the service with effective new data */
        MXS_DEBUG("%lu [cdc_replace_users] users' tables replaced, checksum differs",
                  pthread_self());
        service->users = newusers;
    }

    spinlock_release(&service->spin);

    if (i && oldusers)
    {
        /* free the old table */
        users_free(oldusers);
    }
    return i;
}
