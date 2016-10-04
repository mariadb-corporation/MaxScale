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
 * @file http_ba_auth.c
 *
 * MaxScale HTTP Basic Access authentication for the HTTPD protocol module.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 20/07/2016   Markus Makela           Initial version
 *
 * @endverbatim
 */

#include <gw_authenticator.h>
#include <maxscale/alloc.h>
#include <modinfo.h>
#include <dcb.h>
#include <buffer.h>
#include <openssl/bio.h>
#include <service.h>
#include <secrets.h>
#include <users.h>

/* @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */
MODULE_INFO info =
{
    MODULE_API_AUTHENTICATOR,
    MODULE_GA,
    GWAUTHENTICATOR_VERSION,
    "The MaxScale HTTP BA authenticator"
};
/*lint +e14 */

static char *version_str = "V1.1.0";

static int http_auth_set_protocol_data(DCB *dcb, GWBUF *buf);
static bool http_auth_is_client_ssl_capable(DCB *dcb);
static int http_auth_authenticate(DCB *dcb);
static void http_auth_free_client_data(DCB *dcb);

/*
 * The "module object" for mysql client authenticator module.
 */
static GWAUTHENTICATOR MyObject =
{
    NULL,                            /* No create entry point */
    http_auth_set_protocol_data,     /* Extract data into structure   */
    http_auth_is_client_ssl_capable, /* Check if client supports SSL  */
    http_auth_authenticate,          /* Authenticate user credentials */
    http_auth_free_client_data,      /* Free the client data held in DCB */
    NULL,                            /* No destroy entry point */
    users_default_loadusers          /* Load generic users */
};

typedef struct http_auth
{
    char* user;
    char* pw;
}HTTP_AUTH;

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
/* @see function load_module in load_utils.c for explanation of the following
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
 * @brief Authentication of a user/password combination.
 *
 * The validation is already done, the result is returned.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status - always 0 to denote success
 */
static int
http_auth_authenticate(DCB *dcb)
{
    int rval = 1;
    HTTP_AUTH *ses = (HTTP_AUTH*)dcb->data;
    char *user, *pw;
    serviceGetUser(dcb->service, &user, &pw);
    pw = decryptPassword(pw);

    if (ses && strcmp(ses->user, user) == 0 && strcmp(ses->pw, pw) == 0)
    {
        rval = 0;
    }

    free(pw);
    return rval;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * Expects a buffer containing a Base64 encoded username and password
 * contatenated together by a semicolon as is specificed by HTTP Basic
 * Access authentication.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffers containing data from client
 * @return Authentication status - 0 for success, 1 for failure
 */
static int
http_auth_set_protocol_data(DCB *dcb, GWBUF *buf)
{
    int rval = 1;
    char* value = (char*)GWBUF_DATA(buf);
    char* tok = strstr(value, "Basic");
    tok = tok ? strchr(tok, ' ') : NULL;
    if (tok)
    {
        tok++;
        char outbuf[strlen(tok) * 2 + 1];

        BIO *b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

        BIO *bio = BIO_new_mem_buf(tok, -1);
        BIO_push(b64, bio);
        int nread = BIO_read(b64, outbuf, sizeof(outbuf));
        outbuf[nread] = '\0';
        BIO_free_all(b64);
        char *pw_start = strchr(outbuf, ':');
        if (pw_start)
        {
            *pw_start++ = '\0';
            HTTP_AUTH *ses = MXS_MALLOC(sizeof(*ses));
            char* user = MXS_STRDUP(outbuf);
            char* pw = MXS_STRDUP(pw_start);

            if (ses && user && pw)
            {
                ses->user = user;
                ses->pw = pw;
                dcb->data = ses;
                rval = 0;
            }
            else
            {
                MXS_FREE(ses);
                MXS_FREE(user);
                MXS_FREE(pw);
            }
        }
    }

    return rval;
}

/**
 * @brief Determine whether the client is SSL capable
 *
 * Always say that client is not SSL capable. Support for SSL is not yet
 * available.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether client is SSL capable - false
 */
static bool
http_auth_is_client_ssl_capable(DCB *dcb)
{
    return false;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * The authentication data stored in the DCB is a pointer to a HTTP_AUTH
 * structure allocated in http_auth_set_protocol_data().
 *
 * @param dcb Request handler DCB connected to the client
 */
static void
http_auth_free_client_data(DCB *dcb)
{
    HTTP_AUTH *ses = (HTTP_AUTH*)dcb->data;
    MXS_FREE(ses->user);
    MXS_FREE(ses->pw);
    MXS_FREE(ses);
}
