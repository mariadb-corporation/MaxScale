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

#define MXS_MODULE_NAME "HTTPAuth"

#include <maxscale/authenticator.hh>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.h>
#include <openssl/bio.h>
#include <maxscale/service.hh>
#include <maxscale/secrets.h>
#include <maxscale/users.h>

static bool http_auth_set_protocol_data(DCB* dcb, GWBUF* buf);
static bool http_auth_is_client_ssl_capable(DCB* dcb);
static int  http_auth_authenticate(DCB* dcb);
static void http_auth_free_client_data(DCB* dcb);

typedef struct http_auth
{
    char* user;
    char* pw;
} HTTP_AUTH;

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static MXS_AUTHENTICATOR MyObject =
        {
            NULL,                           /* No initialize entry point */
            NULL,                           /* No create entry point */
            http_auth_set_protocol_data,    /* Extract data into structure   */
            http_auth_is_client_ssl_capable,/* Check if client supports SSL  */
            http_auth_authenticate,         /* Authenticate user credentials */
            http_auth_free_client_data,     /* Free the client data held in DCB */
            NULL,                           /* No destroy entry point */
            users_default_loadusers,        /* Load generic users */
            users_default_diagnostic,       /* Default user diagnostic */
            users_default_diagnostic_json,  /* Default user diagnostic */
            NULL                            /* No user reauthentication */
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_AUTHENTICATOR,
            MXS_MODULE_GA,
            MXS_AUTHENTICATOR_VERSION,
            "The MaxScale HTTP BA authenticator",
            "V1.1.0",
            MXS_NO_MODULE_CAPABILITIES,
            &MyObject,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
/*lint +e14 */
}

/**
 * @brief Authentication of a user/password combination.
 *
 * The validation is already done, the result is returned.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status - always 0 to denote success
 */
static int http_auth_authenticate(DCB* dcb)
{
    int rval = 1;
    HTTP_AUTH* ses = (HTTP_AUTH*)dcb->data;
    const char* user;
    const char* password;

    serviceGetUser(dcb->service, &user, &password);
    char* pw = decrypt_password(password);

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
 * concatenated together by a semicolon as is specified by HTTP Basic
 * Access authentication.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffers containing data from client
 * @return Authentication status - true for success, false for failure
 */
static bool http_auth_set_protocol_data(DCB* dcb, GWBUF* buf)
{
    bool rval = false;
    char* value = (char*)GWBUF_DATA(buf);
    char* tok = strstr(value, "Basic");
    tok = tok ? strchr(tok, ' ') : NULL;
    if (tok)
    {
        tok++;
        char outbuf[strlen(tok) * 2 + 1];

        BIO* b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

        BIO* bio = BIO_new_mem_buf(tok, -1);
        BIO_push(b64, bio);
        int nread = BIO_read(b64, outbuf, sizeof(outbuf));
        outbuf[nread] = '\0';
        BIO_free_all(b64);
        char* pw_start = strchr(outbuf, ':');
        if (pw_start)
        {
            *pw_start++ = '\0';
            HTTP_AUTH* ses = static_cast<HTTP_AUTH*>(MXS_MALLOC(sizeof(*ses)));
            char* user = MXS_STRDUP(outbuf);
            char* pw = MXS_STRDUP(pw_start);

            if (ses && user && pw)
            {
                ses->user = user;
                ses->pw = pw;
                dcb->data = ses;
                rval = true;
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
static bool http_auth_is_client_ssl_capable(DCB* dcb)
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
static void http_auth_free_client_data(DCB* dcb)
{
    HTTP_AUTH* ses = (HTTP_AUTH*)dcb->data;
    MXS_FREE(ses->user);
    MXS_FREE(ses->pw);
    MXS_FREE(ses);
}
