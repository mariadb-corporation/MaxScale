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
 * @file mysql_auth.c
 *
 * MySQL Authentication module for handling the checking of clients credentials
 * in the MySQL protocol.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 02/02/2016   Martin Brampton         Initial version
 *
 * @endverbatim
 */

#include <mysql_auth.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/gw_authenticator.h>
#include <maxscale/alloc.h>
#include <maxscale/poll.h>
#include <maxscale/dbusers.h>
#include <maxscale/gwdirs.h>
#include <maxscale/secrets.h>
#include <maxscale/utils.h>

typedef struct mysql_auth
{
    char *cache_dir;          /**< Custom cache directory location */
    bool inject_service_user; /**< Inject the service user into the list of users */
} MYSQL_AUTH;


/* @see function load_module in load_utils.c for explanation of the following
 * lint directives.
*/
/*lint -e14 */
MODULE_INFO info =
{
    MODULE_API_AUTHENTICATOR,
    MODULE_GA,
    GWAUTHENTICATOR_VERSION,
    "The MySQL client to MaxScale authenticator implementation"
};
/*lint +e14 */

static char *version_str = "V1.1.0";

static void* mysql_auth_init(char **options);
static int mysql_auth_set_protocol_data(DCB *dcb, GWBUF *buf);
static bool mysql_auth_is_client_ssl_capable(DCB *dcb);
static int mysql_auth_authenticate(DCB *dcb);
static void mysql_auth_free_client_data(DCB *dcb);
static int mysql_auth_load_users(SERV_LISTENER *port);

/*
 * The "module object" for mysql client authenticator module.
 */
static GWAUTHENTICATOR MyObject =
{
    mysql_auth_init,                  /* Initialize the authenticator */
    NULL,                             /* No create entry point */
    mysql_auth_set_protocol_data,     /* Extract data into structure   */
    mysql_auth_is_client_ssl_capable, /* Check if client supports SSL  */
    mysql_auth_authenticate,          /* Authenticate user credentials */
    mysql_auth_free_client_data,      /* Free the client data held in DCB */
    NULL,                             /* No destroy entry point */
    mysql_auth_load_users             /* Load users from backend databases */
};

static int combined_auth_check(
    DCB             *dcb,
    uint8_t         *auth_token,
    size_t          auth_token_len,
    MySQLProtocol   *protocol,
    char            *username,
    uint8_t         *stage1_hash,
    char            *database
);
static int mysql_auth_set_client_data(
    MYSQL_session *client_data,
    MySQLProtocol *protocol,
    GWBUF         *buffer);

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 *
 * @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */
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
/*lint +e14 */

/**
 * @brief Initialize the authenticator instance
 *
 * @param options Authenticator options
 * @return New MYSQL_AUTH instance or NULL on error
 */
static void* mysql_auth_init(char **options)
{
    MYSQL_AUTH *instance = MXS_MALLOC(sizeof(*instance));

    if (instance)
    {
        bool error = false;
        instance->cache_dir = NULL;
        instance->inject_service_user = true;

        for (int i = 0; options[i]; i++)
        {
            char *value = strchr(options[i], '=');

            if (value)
            {
                *value++ = '\0';

                if (strcmp(options[i], "cache_dir") == 0)
                {
                    if ((instance->cache_dir = MXS_STRDUP(value)) == NULL)
                    {
                        error = true;
                    }
                }
                else if (strcmp(options[i], "inject_service_user") == 0)
                {
                    instance->inject_service_user = config_truth_value(value);
                }
                else
                {
                    MXS_ERROR("Unknown authenticator option: %s", options[i]);
                    error = true;
                }
            }
            else
            {
                MXS_ERROR("Unknown authenticator option: %s", options[i]);
                error = true;
            }
        }

        if (error)
        {
            MXS_FREE(instance->cache_dir);
            MXS_FREE(instance);
            instance = NULL;
        }
    }

    return instance;
}

/**
 * @brief Authenticates a MySQL user who is a client to MaxScale.
 *
 * First call the SSL authentication function, passing the DCB and a boolean
 * indicating whether the client is SSL capable. If SSL authentication is
 * successful, check whether connection is complete. Fail if we do not have a
 * user name.  Call other functions to validate the user, reloading the user
 * data if the first attempt fails.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 */
static int
mysql_auth_authenticate(DCB *dcb)
{
    MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    MYSQL_session *client_data = (MYSQL_session *)dcb->data;
    int auth_ret;

    /**
     * We record the SSL status before and after the authentication. This allows
     * us to detect if the SSL handshake is immediately completed which means more
     * data needs to be read from the socket.
     */

    bool health_before = ssl_is_connection_healthy(dcb);
    int ssl_ret = ssl_authenticate_client(dcb, dcb->authfunc.connectssl(dcb));
    bool health_after = ssl_is_connection_healthy(dcb);

    if (0 != ssl_ret)
    {
        auth_ret = (SSL_ERROR_CLIENT_NOT_SSL == ssl_ret) ? MXS_AUTH_FAILED_SSL : MXS_AUTH_FAILED;
    }

    else if (!health_after)
    {
        auth_ret = MXS_AUTH_SSL_INCOMPLETE;
    }

    else if (!health_before && health_after)
    {
        auth_ret = MXS_AUTH_SSL_INCOMPLETE;
        poll_add_epollin_event_to_dcb(dcb, NULL);
    }

    else if (0 == strlen(client_data->user))
    {
        auth_ret = MXS_AUTH_FAILED;
    }

    else
    {
        MXS_DEBUG("Receiving connection from '%s' to database '%s'.",
                  client_data->user, client_data->db);

        auth_ret = combined_auth_check(dcb, client_data->auth_token, client_data->auth_token_len,
                                       protocol, client_data->user, client_data->client_sha1, client_data->db);

        /* On failed authentication try to load user table from backend database */
        /* Success for service_refresh_users returns 0 */
        if (MXS_AUTH_SUCCEEDED != auth_ret && 0 == service_refresh_users(dcb->service))
        {
            auth_ret = combined_auth_check(dcb, client_data->auth_token, client_data->auth_token_len, protocol,
                                           client_data->user, client_data->client_sha1, client_data->db);
        }

        /* on successful authentication, set user into dcb field */
        if (MXS_AUTH_SUCCEEDED == auth_ret)
        {
            dcb->user = MXS_STRDUP_A(client_data->user);
            /** Send an OK packet to the client */
        }
        else if (dcb->service->log_auth_warnings)
        {
            MXS_NOTICE("%s: login attempt for user '%s'@%s:%d, authentication failed.",
                       dcb->service->name, client_data->user, dcb->remote, ntohs(dcb->ipv4.sin_port));
            if (dcb->ipv4.sin_addr.s_addr == 0x0100007F &&
                !dcb->service->localhost_match_wildcard_host)
            {
                MXS_NOTICE("If you have a wildcard grant that covers"
                           " this address, try adding "
                           "'localhost_match_wildcard_host=true' for "
                           "service '%s'. ", dcb->service->name);
            }
        }

        /* let's free the auth_token now */
        if (client_data->auth_token)
        {
            MXS_FREE(client_data->auth_token);
            client_data->auth_token = NULL;
        }
    }

    return auth_ret;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * The request handler DCB has a field called data that contains protocol
 * specific information. This function examines a buffer containing MySQL
 * authentication data and puts it into a structure that is referred to
 * by the DCB. If the information in the buffer is invalid, then a failure
 * code is returned. A call to mysql_auth_set_client_data does the
 * detailed work.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffer containing data from client
 * @return Authentication status
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 * @see https://dev.mysql.com/doc/internals/en/client-server-protocol.html
 */
static int
mysql_auth_set_protocol_data(DCB *dcb, GWBUF *buf)
{
    uint8_t *client_auth_packet = GWBUF_DATA(buf);
    MySQLProtocol *protocol = NULL;
    MYSQL_session *client_data = NULL;
    int client_auth_packet_size = 0;

    protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    CHK_PROTOCOL(protocol);

    client_data = (MYSQL_session *)dcb->data;

    client_auth_packet_size = gwbuf_length(buf);

    /* For clients supporting CLIENT_PROTOCOL_41
     * the Handshake Response Packet is:
     *
     * 4            bytes mysql protocol heade
     * 4            bytes capability flags
     * 4            max-packet size
     * 1            byte character set
     * string[23]   reserved (all [0])
     * ...
     * ...
     * Note that the fixed elements add up to 36
     */

    /* Detect now if there are enough bytes to continue */
    if (client_auth_packet_size < (4 + 4 + 4 + 1 + 23))
    {
        /* Packet is not big enough */
        return MXS_AUTH_FAILED;
    }

    return mysql_auth_set_client_data(client_data, protocol, buf);
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
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 * @see https://dev.mysql.com/doc/internals/en/client-server-protocol.html
 */
static int
mysql_auth_set_client_data(
    MYSQL_session *client_data,
    MySQLProtocol *protocol,
    GWBUF         *buffer)
{
    size_t client_auth_packet_size = gwbuf_length(buffer);
    uint8_t client_auth_packet[client_auth_packet_size];
    gwbuf_copy_data(buffer, 0, client_auth_packet_size, client_auth_packet);

    int packet_length_used = 0;

    /* Make authentication token length 0 and token null in case none is provided */
    client_data->auth_token_len = 0;
    client_data->auth_token = NULL;

    if (client_auth_packet_size > MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        /* Should have a username */
        char *first_letter_of_username = (char *)(client_auth_packet + MYSQL_AUTH_PACKET_BASE_SIZE);
        int user_length = strlen(first_letter_of_username);

        ss_dassert(client_auth_packet_size > (MYSQL_AUTH_PACKET_BASE_SIZE + user_length)
                   && user_length <= MYSQL_USER_MAXLEN);

        if (client_auth_packet_size > (MYSQL_AUTH_PACKET_BASE_SIZE + user_length + 1))
        {
            /* Extra 1 is for the terminating null after user name */
            packet_length_used = MYSQL_AUTH_PACKET_BASE_SIZE + user_length + 1;
            /* We should find an authentication token next */
            /* One byte of packet is the length of authentication token */
            memcpy(&client_data->auth_token_len,
                   client_auth_packet + packet_length_used, 1);

            if (client_auth_packet_size >
                (packet_length_used + client_data->auth_token_len))
            {
                /* Packet is large enough for authentication token */
                if (NULL != (client_data->auth_token = (uint8_t *)MXS_MALLOC(client_data->auth_token_len)))
                {
                    /* The extra 1 is for the token length byte, just extracted*/
                    memcpy(client_data->auth_token,
                           client_auth_packet + MYSQL_AUTH_PACKET_BASE_SIZE + user_length + 1 + 1,
                           client_data->auth_token_len);
                }
                else
                {
                    /* Failed to allocate space for authentication token string */
                    return MXS_AUTH_FAILED;
                }
            }
            else
            {
                /* Packet was too small to contain authentication token */
                return MXS_AUTH_FAILED;
            }
        }
    }
    return MXS_AUTH_SUCCEEDED;
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
mysql_auth_is_client_ssl_capable(DCB *dcb)
{
    MySQLProtocol *protocol;

    protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    return (protocol->client_capabilities & (int)GW_MYSQL_CAPABILITIES_SSL) ? true : false;
}

/**
 * gw_find_mysql_user_password_sha1
 *
 * The routine fetches an user from the MaxScale users' table
 * The users' table is dcb->listener->users or a different one specified with void *repository
 * The user lookup uses username,host and db name (if passed in connection or change user)
 *
 * If found the HEX password, representing sha1(sha1(password)), is converted in binary data and
 * copied into gateway_password
 *
 * @param username              The user to look for
 * @param gateway_password      The related SHA1(SHA1(password)), the pointer must be preallocated
 * @param dcb                   Current DCB
 * @return 1 if user is not found or 0 if the user exists
 *
 */
int gw_find_mysql_user_password_sha1(char *username, uint8_t *gateway_password, DCB *dcb)
{
    MYSQL_session *client_data = (MYSQL_session *) dcb->data;
    SERVICE *service = (SERVICE *) dcb->service;
    SERV_LISTENER *listener = dcb->listener;
    struct sockaddr_in *client = (struct sockaddr_in *) &dcb->ipv4;

    MYSQL_USER_HOST key = {};
    key.user = username;
    memcpy(&key.ipv4, client, sizeof(struct sockaddr_in));
    key.netmask = 32;
    key.resource = client_data->db;

    if (strlen(dcb->remote) < MYSQL_HOST_MAXLEN)
    {
        strcpy(key.hostname, dcb->remote);
    }

    MXS_DEBUG("%lu [MySQL Client Auth], checking user [%s@%s]%s%s",
              pthread_self(),
              key.user,
              dcb->remote,
              key.resource != NULL ? " db: " : "",
              key.resource != NULL ? key.resource : "");

    /* look for user@current_ipv4 now */
    char *user_password = mysql_users_fetch(listener->users, &key);

    if (!user_password)
    {
        /* The user is not authenticated @ current IPv4 */

        while (1)
        {
            /*
             * (1) Check for localhost first: 127.0.0.1 (IPv4 only)
             */

            if ((key.ipv4.sin_addr.s_addr == 0x0100007F) &&
                !dcb->service->localhost_match_wildcard_host)
            {
                /* Skip the wildcard check and return 1 */
                break;
            }

            /*
             * (2) check for possible IPv4 class C,B,A networks
             */

            /* Class C check */
            key.ipv4.sin_addr.s_addr &= 0x00FFFFFF;
            key.netmask -= 8;

            user_password = mysql_users_fetch(listener->users, &key);

            if (user_password)
            {
                break;
            }

            /* Class B check */
            key.ipv4.sin_addr.s_addr &= 0x0000FFFF;
            key.netmask -= 8;

            user_password = mysql_users_fetch(listener->users, &key);

            if (user_password)
            {
                break;
            }

            /* Class A check */
            key.ipv4.sin_addr.s_addr &= 0x000000FF;
            key.netmask -= 8;

            user_password = mysql_users_fetch(listener->users, &key);

            if (user_password)
            {
                break;
            }

            /*
             * (3) Continue check for wildcard host, user@%
             */

            memset(&key.ipv4, 0, sizeof(struct sockaddr_in));
            key.netmask = 0;

            MXS_DEBUG("%lu [MySQL Client Auth], checking user [%s@%s] with "
                      "wildcard host [%%]",
                      pthread_self(),
                      key.user,
                      dcb->remote);

            user_password = mysql_users_fetch(listener->users, &key);

            if (user_password)
            {
                break;
            }

            if (!user_password)
            {
                /*
                 * user@% not found.
                 */

                MXS_DEBUG("%lu [MySQL Client Auth], user [%s@%s] not existent",
                          pthread_self(),
                          key.user,
                          dcb->remote);

                MXS_INFO("Authentication Failed: user [%s@%s] not found.",
                         key.user,
                         dcb->remote);
                break;
            }

        }
    }

    /* If user@host has been found we get the the password in binary format*/
    if (user_password)
    {
        /*
         * Convert the hex data (40 bytes) to binary (20 bytes).
         * The gateway_password represents the SHA1(SHA1(real_password)).
         * Please note: the real_password is unknown and SHA1(real_password) is unknown as well
         */
        int passwd_len = strlen(user_password);
        if (passwd_len)
        {
            passwd_len = (passwd_len <= (SHA_DIGEST_LENGTH * 2)) ? passwd_len : (SHA_DIGEST_LENGTH * 2);
            gw_hex2bin(gateway_password, user_password, passwd_len);
        }

        return 0;
    }
    else
    {
        return 1;
    }
}

/**
 *
 * @brief Check authentication token received against stage1_hash and scramble
 *
 * @param dcb The current dcb
 * @param token         The token sent by the client in the authentication request
 * @param token_len     The token size in bytes
 * @param scramble      The scramble data sent by the server during handshake
 * @param scramble_len  The scramble size in bytes
 * @param username      The current username in the authentication request
 * @param stage1_hash   The SHA1(candidate_password) decoded by this routine
 * @return Authentication status
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 *
 */
int
gw_check_mysql_scramble_data(DCB *dcb,
                             uint8_t *token,
                             unsigned int token_len,
                             uint8_t *mxs_scramble,
                             unsigned int scramble_len,
                             char *username,
                             uint8_t *stage1_hash)
{
    uint8_t step1[GW_MYSQL_SCRAMBLE_SIZE] = "";
    uint8_t step2[GW_MYSQL_SCRAMBLE_SIZE + 1] = "";
    uint8_t check_hash[GW_MYSQL_SCRAMBLE_SIZE] = "";
    char hex_double_sha1[2 * GW_MYSQL_SCRAMBLE_SIZE + 1] = "";
    uint8_t password[GW_MYSQL_SCRAMBLE_SIZE] = "";
    /* The following can be compared using memcmp to detect a null password */
    uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN] = "";


    if ((username == NULL) || (mxs_scramble == NULL) || (stage1_hash == NULL))
    {
        return MXS_AUTH_FAILED;
    }

    /*<
     * get the user's password from repository in SHA1(SHA1(real_password));
     * please note 'real_password' is unknown!
     */

    if (gw_find_mysql_user_password_sha1(username, password, dcb))
    {
        /* if password was sent, fill stage1_hash with at least 1 byte in order
         * to create right error message: (using password: YES|NO)
         */
        if (token_len)
        {
            memcpy(stage1_hash, (char *)"_", 1);
        }

        return MXS_AUTH_FAILED;
    }

    if (token && token_len)
    {
        /*<
         * convert in hex format: this is the content of mysql.user table.
         * The field password is without the '*' prefix and it is 40 bytes long
         */

        gw_bin2hex(hex_double_sha1, password, SHA_DIGEST_LENGTH);
    }
    else
    {
        /* check if the password is not set in the user table */
        return memcmp(password, null_client_sha1, MYSQL_SCRAMBLE_LEN) ?
               MXS_AUTH_FAILED : MXS_AUTH_SUCCEEDED;
    }

    /*<
     * Auth check in 3 steps
     *
     * Note: token = XOR (SHA1(real_password), SHA1(CONCAT(scramble, SHA1(SHA1(real_password)))))
     * the client sends token
     *
     * Now, server side:
     *
     *
     * step 1: compute the STEP1 = SHA1(CONCAT(scramble, gateway_password))
     * the result in step1 is SHA_DIGEST_LENGTH long
     */

    gw_sha1_2_str(mxs_scramble, scramble_len, password, SHA_DIGEST_LENGTH, step1);

    /*<
     * step2: STEP2 = XOR(token, STEP1)
     *
     * token is transmitted form client and it's based on the handshake scramble and SHA1(real_passowrd)
     * step1 has been computed in the previous step
     * the result STEP2 is SHA1(the_password_to_check) and is SHA_DIGEST_LENGTH long
     */

    gw_str_xor(step2, token, step1, token_len);

    /*<
     * copy the stage1_hash back to the caller
     * stage1_hash will be used for backend authentication
     */

    memcpy(stage1_hash, step2, SHA_DIGEST_LENGTH);

    /*<
     * step 3: prepare the check_hash
     *
     * compute the SHA1(STEP2) that is SHA1(SHA1(the_password_to_check)), and is SHA_DIGEST_LENGTH long
     */

    gw_sha1_str(step2, SHA_DIGEST_LENGTH, check_hash);


#ifdef GW_DEBUG_CLIENT_AUTH
    {
        char inpass[128] = "";
        gw_bin2hex(inpass, check_hash, SHA_DIGEST_LENGTH);

        fprintf(stderr, "The CLIENT hex(SHA1(SHA1(password))) for \"%s\" is [%s]", username, inpass);
    }
#endif

    /* now compare SHA1(SHA1(gateway_password)) and check_hash: return 0 is MYSQL_AUTH_OK */
    return (0 == memcmp(password, check_hash, SHA_DIGEST_LENGTH)) ?
           MXS_AUTH_SUCCEEDED : MXS_AUTH_FAILED;
}

/**
 * @brief If the client connection specifies a database, check existence
 *
 * The client can specify a default database, but if so, it must be one
 * that exists. This function is chained from the previous one, and will
 * amend the given return code if it is previously showing success.
 *
 * @param dcb Request handler DCB connected to the client
 * @param database A string containing the database name
 * @param auth_ret The authentication status prior to calling this function.
 * @return Authentication status
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 */
int
check_db_name_after_auth(DCB *dcb, char *database, int auth_ret)
{
    int db_exists = -1;

    /* check for database name and possible match in resource hashtable */
    if (database && strlen(database))
    {
        /* if database names are loaded we can check if db name exists */
        if (dcb->listener->resources != NULL)
        {
            if (hashtable_fetch(dcb->listener->resources, database))
            {
                db_exists = 1;
            }
            else
            {
                db_exists = 0;
            }
        }
        else
        {
            /* if database names are not loaded we don't allow connection with db name*/
            db_exists = -1;
        }

        if (db_exists == 0 && auth_ret == MXS_AUTH_SUCCEEDED)
        {
            auth_ret = MXS_AUTH_FAILED_DB;
        }

        if (db_exists < 0 && auth_ret == MXS_AUTH_SUCCEEDED)
        {
            auth_ret = MXS_AUTH_FAILED;
        }
    }

    return auth_ret;
}

/**
 * @brief Function to easily call authentication and database checks.
 *
 * The two functions are called one after the other, with the return from
 * the first passed to the second. For convenience and clarity this function
 * combines the calls.
 *
 * @param dcb Request handler DCB connected to the client
 * @param auth_token A string of bytes containing the authentication token
 * @param auth_token_len An integer, the length of the preceding parameter
 * @param protocol  The protocol structure for the connection
 * @param username  String containing username
 * @param stage1_hash A password hash for authentication
 * @param database A string containing the name for the default database
 * @return Authentication status
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 */
static int combined_auth_check(
    DCB             *dcb,
    uint8_t         *auth_token,
    size_t          auth_token_len,
    MySQLProtocol   *protocol,
    char            *username,
    uint8_t         *stage1_hash,
    char            *database
)
{
    int     auth_ret;

    auth_ret = gw_check_mysql_scramble_data(dcb,
                                            auth_token,
                                            auth_token_len,
                                            protocol->scramble,
                                            sizeof(protocol->scramble),
                                            username,
                                            stage1_hash);

    /* check for database name match in resource hashtable */
    auth_ret = check_db_name_after_auth(dcb, database, auth_ret);
    return auth_ret;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * Currently all that is required is to free the storage pointed to by
 * dcb->data.  But this is intended to be implemented as part of the
 * authentication API at which time this code will be moved into the
 * MySQL authenticator.  If the data structure were to become more complex
 * the mechanism would still work and be the responsibility of the authenticator.
 * The DCB should not know authenticator implementation details.
 *
 * @param dcb Request handler DCB connected to the client
 */
static void
mysql_auth_free_client_data(DCB *dcb)
{
    MXS_FREE(dcb->data);
}

/**
 * @brief Inject the service user into the cache
 *
 * @param port Service listener
 * @return True on success, false on error
 */
static bool add_service_user(SERV_LISTENER *port)
{
    char *user = NULL;
    char *pw = NULL;
    bool rval = false;

    if (serviceGetUser(port->service, &user, &pw))
    {
        pw = decryptPassword(pw);

        if (pw)
        {
            char *newpw = create_hex_sha1_sha1_passwd(pw);

            if (newpw)
            {
                add_mysql_users_with_host_ipv4(port->users, user, "%", newpw, "Y", "");
                add_mysql_users_with_host_ipv4(port->users, user, "localhost", newpw, "Y", "");
                MXS_FREE(newpw);
                rval = true;
            }
            MXS_FREE(pw);
        }
        else
        {
            MXS_ERROR("[%s] Failed to decrypt service user password.", port->service->name);
        }
    }
    else
    {
        MXS_ERROR("[%s] Failed to retrieve service credentials.", port->service->name);
    }

    return rval;
}

/**
 * @brief Load MySQL authentication users
 *
 * This function loads MySQL users from the backend database.
 *
 * @param port Listener definition
 * @return AUTH_LOADUSERS_OK on success, AUTH_LOADUSERS_ERROR on error
 */
static int mysql_auth_load_users(SERV_LISTENER *port)
{
    int rc = MXS_AUTH_LOADUSERS_OK;
    SERVICE *service = port->listener->service;
    MYSQL_AUTH *instance = (MYSQL_AUTH*)port->auth_instance;
    int loaded = replace_mysql_users(port);
    char path[PATH_MAX];

    if (instance->cache_dir)
    {
        strcpy(path, instance->cache_dir);
    }
    else
    {
        sprintf(path, "%s/%s/%s/%s/", get_cachedir(), service->name, port->name, DBUSERS_DIR);
    }

    if (loaded < 0)
    {
        MXS_ERROR("[%s] Unable to load users for listener %s listening at %s:%d.", service->name,
                  port->name, port->address ? port->address : "0.0.0.0", port->port);

        strcat(path, DBUSERS_FILE);

        if ((loaded = dbusers_load(port->users, path)) == -1)
        {
            MXS_ERROR("[%s] Failed to load cached users from '%s'.", service->name, path);
            rc = MXS_AUTH_LOADUSERS_ERROR;
        }
        else
        {
            MXS_WARNING("Using cached credential information.");
        }

        if (instance->inject_service_user)
        {
            /** Inject the service user as a 'backup' user that's available
             * if loading of the users fails */
            if (!add_service_user(port))
            {
                MXS_ERROR("[%s] Failed to inject service user.", port->service->name);
            }
        }
    }
    else
    {
        /* Users loaded successfully, save authentication data to file cache */
        if (mxs_mkdir_all(path, 0777))
        {
            strcat(path, DBUSERS_FILE);
            dbusers_save(port->users, path);
        }
    }

    if (loaded == 0)
    {
        MXS_WARNING("[%s]: failed to load any user information. Authentication"
                    " will probably fail as a result.", service->name);
    }
    else if (loaded > 0)
    {
        MXS_NOTICE("[%s] Loaded %d MySQL users for listener %s.", service->name, loaded, port->name);
    }

    return rc;
}
