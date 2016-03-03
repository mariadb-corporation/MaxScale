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
#include <mysql_client_server_protocol.h>

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
    uint8_t *client_auth_packet,
    int client_auth_packet_size);

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
 * @param buffer Pointer to pointer to buffer containing data from client
 * @return Authentication status
 * @note Authentication status codes are defined in mysql_client_server_protocol.h
 */
int
mysql_auth_authenticate(DCB *dcb, GWBUF **buffer)
{
    MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    MYSQL_session *client_data = (MYSQL_session *)dcb->data;
    int auth_ret, ssl_ret;

    if (0 != (ssl_ret = ssl_authenticate_client(dcb, client_data->user, mysql_auth_is_client_ssl_capable(dcb))))
    {
        auth_ret = (SSL_ERROR_CLIENT_NOT_SSL == ssl_ret) ? MYSQL_FAILED_AUTH_SSL : MYSQL_FAILED_AUTH;
    }

    else if (!ssl_is_connection_healthy(dcb))
    {
        auth_ret = MYSQL_AUTH_SSL_INCOMPLETE;
    }

    else if (0 == strlen(client_data->user))
    {
        auth_ret = MYSQL_FAILED_AUTH;
    }

    else
    {
        MXS_DEBUG("Receiving connection from '%s' to database '%s'.",
            client_data->user, client_data->db);

        auth_ret = combined_auth_check(dcb, client_data->auth_token, client_data->auth_token_len,
            protocol, client_data->user, client_data->client_sha1, client_data->db);

        /* On failed authentication try to load user table from backend database */
        /* Success for service_refresh_users returns 0 */
        if (MYSQL_AUTH_SUCCEEDED != auth_ret && 0 == service_refresh_users(dcb->service))
        {
            auth_ret = combined_auth_check(dcb, client_data->auth_token, client_data->auth_token_len, protocol,
                client_data->user, client_data->client_sha1, client_data->db);
        }

        /* on successful authentication, set user into dcb field */
        if (MYSQL_AUTH_SUCCEEDED == auth_ret)
        {
            dcb->user = strdup(client_data->user);
        }
        else if (dcb->service->log_auth_warnings)
        {
            MXS_NOTICE("%s: login attempt for user '%s', authentication failed.",
                   dcb->service->name, client_data->user);
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
            free(client_data->auth_token);
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
 * @note Authentication status codes are defined in mysql_client_server_protocol.h
 * @see https://dev.mysql.com/doc/internals/en/client-server-protocol.html
 */
int
mysql_auth_set_protocol_data(DCB *dcb, GWBUF *buf)
{
    uint8_t *client_auth_packet = GWBUF_DATA(buf);
    MySQLProtocol *protocol = NULL;
    MYSQL_session *client_data = NULL;
    int client_auth_packet_size = 0;

    protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    CHK_PROTOCOL(protocol);
    if (dcb->data == NULL)
    {
        if (NULL == (client_data = (MYSQL_session *)calloc(1, sizeof(MYSQL_session))))
        {
            return MYSQL_FAILED_AUTH;
        }
#if defined(SS_DEBUG)
        client_data->myses_chk_top = CHK_NUM_MYSQLSES;
        client_data->myses_chk_tail = CHK_NUM_MYSQLSES;
#endif
        dcb->data = client_data;
    }
    else
    {
        client_data = (MYSQL_session *)dcb->data;
    }

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
        return MYSQL_FAILED_AUTH;
    }

    return mysql_auth_set_client_data(client_data, protocol, client_auth_packet,
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
 * @note Authentication status codes are defined in mysql_client_server_protocol.h
 * @see https://dev.mysql.com/doc/internals/en/client-server-protocol.html
 */
static int
mysql_auth_set_client_data(
    MYSQL_session *client_data,
    MySQLProtocol *protocol,
    uint8_t *client_auth_packet,
    int client_auth_packet_size)
{
    /* The numbers are the fixed elements in the client handshake packet */
    int auth_packet_base_size = 4 + 4 + 4 + 1 + 23;
    int packet_length_used = 0;

    /* Take data from fixed locations first */
    memcpy(&protocol->client_capabilities, client_auth_packet + 4, 4);
    protocol->charset = 0;
    memcpy(&protocol->charset, client_auth_packet + 4 + 4 + 4, 1);

    /* Make username and database a null string in case none is provided */
    client_data->user[0] = 0;
    client_data->db[0] = 0;
    /* Make authentication token length 0 and token null in case none is provided */
    client_data->auth_token_len = 0;
    client_data->auth_token = NULL;

    if (client_auth_packet_size > auth_packet_base_size)
    {
        /* Should have a username */
        char *first_letter_of_username = (char *)(client_auth_packet + auth_packet_base_size);
        int user_length = strlen(first_letter_of_username);
        if (client_auth_packet_size > (auth_packet_base_size + user_length)
            && user_length <= MYSQL_USER_MAXLEN)
        {
            strcpy(client_data->user, first_letter_of_username);
        }
        else
        {
            /* Packet has incomplete or too long username */
            return MYSQL_FAILED_AUTH;
        }
        if (client_auth_packet_size > (auth_packet_base_size + user_length + 1))
        {
            /* Extra 1 is for the terminating null after user name */
            packet_length_used = auth_packet_base_size + user_length + 1;
            /* We should find an authentication token next */
            /* One byte of packet is the length of authentication token */
            memcpy(&client_data->auth_token_len,
                client_auth_packet + packet_length_used,
                1);
            if (client_auth_packet_size >
                (packet_length_used + client_data->auth_token_len))
            {
                /* Packet is large enough for authentication token */
                if (NULL != (client_data->auth_token = (uint8_t *)malloc(client_data->auth_token_len)))
                {
                    /* The extra 1 is for the token length byte, just extracted*/
                    memcpy(client_data->auth_token,
                        client_auth_packet + auth_packet_base_size + user_length + 1 +1,
                        client_data->auth_token_len);
                }
                else
                {
                    /* Failed to allocate space for authentication token string */
                    return MYSQL_FAILED_AUTH;
                }
            }
            else
            {
                /* Packet was too small to contain authentication token */
                return MYSQL_FAILED_AUTH;
            }
            packet_length_used += 1 + client_data->auth_token_len;
            /*
             * Note: some clients may pass empty database, CONNECT_WITH_DB !=0 but database =""
             */
            if (GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB &
                gw_mysql_get_byte4((uint32_t *)&protocol->client_capabilities)
                && client_auth_packet_size > packet_length_used)
            {
                char *database = (char *)(client_auth_packet + packet_length_used);
                int database_length = strlen(database);
                if (client_auth_packet_size >
                    (packet_length_used + database_length)
                    && strlen(database) <= MYSQL_DATABASE_MAXLEN)
                {
                    strcpy(client_data->db, database);
                }
                else
                {
                    /* Packet is too short to contain database string */
                    /* or database string in packet is too long */
                    return MYSQL_FAILED_AUTH;
                }
            }
        }
    }
    return MYSQL_AUTH_SUCCEEDED;
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
bool
mysql_auth_is_client_ssl_capable(DCB *dcb)
{
    MySQLProtocol *protocol;

    protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    return (protocol->client_capabilities & GW_MYSQL_CAPABILITIES_SSL) ? true : false;
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
 * @note Authentication status codes are defined in mysql_client_server_protocol.h
 *
 */
int
gw_check_mysql_scramble_data(DCB *dcb,
                                 uint8_t *token,
                                 unsigned int token_len,
                                 uint8_t *scramble,
                                 unsigned int scramble_len,
                                 char *username,
                                 uint8_t *stage1_hash)
{
    uint8_t step1[GW_MYSQL_SCRAMBLE_SIZE]="";
    uint8_t step2[GW_MYSQL_SCRAMBLE_SIZE +1]="";
    uint8_t check_hash[GW_MYSQL_SCRAMBLE_SIZE]="";
    char hex_double_sha1[2 * GW_MYSQL_SCRAMBLE_SIZE + 1]="";
    uint8_t password[GW_MYSQL_SCRAMBLE_SIZE]="";
    /* The following can be compared using memcmp to detect a null password */
    uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN]="";


    if ((username == NULL) || (scramble == NULL) || (stage1_hash == NULL))
    {
        return MYSQL_FAILED_AUTH;
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
            memcpy(stage1_hash, (char *)"_", 1);

        return MYSQL_FAILED_AUTH;
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
            MYSQL_FAILED_AUTH : MYSQL_AUTH_SUCCEEDED;
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

    gw_sha1_2_str(scramble, scramble_len, password, SHA_DIGEST_LENGTH, step1);

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
        char inpass[128]="";
        gw_bin2hex(inpass, check_hash, SHA_DIGEST_LENGTH);

        fprintf(stderr, "The CLIENT hex(SHA1(SHA1(password))) for \"%s\" is [%s]", username, inpass);
    }
#endif

    /* now compare SHA1(SHA1(gateway_password)) and check_hash: return 0 is MYSQL_AUTH_OK */
    return (0 == memcmp(password, check_hash, SHA_DIGEST_LENGTH)) ?
        MYSQL_AUTH_SUCCEEDED : MYSQL_FAILED_AUTH;
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
 * @note Authentication status codes are defined in mysql_client_server_protocol.h
 */
int
check_db_name_after_auth(DCB *dcb, char *database, int auth_ret)
{
    int db_exists = -1;

    /* check for database name and possible match in resource hashtable */
    if (database && strlen(database))
    {
        /* if database names are loaded we can check if db name exists */
        if (dcb->service->resources != NULL)
        {
            if (hashtable_fetch(dcb->service->resources, database))
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

        if (db_exists == 0 && auth_ret == MYSQL_AUTH_SUCCEEDED)
        {
            auth_ret = MYSQL_FAILED_AUTH_DB;
        }

        if (db_exists < 0 && auth_ret == MYSQL_AUTH_SUCCEEDED)
        {
            auth_ret = MYSQL_FAILED_AUTH;
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
 * @note Authentication status codes are defined in mysql_client_server_protocol.h
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

