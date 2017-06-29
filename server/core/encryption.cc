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

#include <maxscale/cppdefs.hh>

#include <maxscale/alloc.h>
#include <maxscale/encryption.h>

EVP_CIPHER_CTX* mxs_evp_cipher_ctx_alloc()
{
#ifdef OPENSSL_1_1
    return EVP_CIPHER_CTX_new();
#else
    EVP_CIPHER_CTX* rval = (EVP_CIPHER_CTX*)MXS_MALLOC(sizeof(*rval));
    EVP_CIPHER_CTX_init(rval);
    return rval;
#endif
}

void mxs_evp_cipher_ctx_free(EVP_CIPHER_CTX* ctx)
{
#ifdef OPENSSL_1_1
    EVP_CIPHER_CTX_free(ctx);
#else
    MXS_FREE(ctx);
#endif
}

uint8_t* mxs_evp_cipher_ctx_buf(EVP_CIPHER_CTX* ctx)
{
#ifdef OPENSSL_1_1
    return (uint8_t*)EVP_CIPHER_CTX_buf_noconst(ctx);
#else
    return (uint8_t*)ctx->buf;
#endif
}

uint8_t* mxs_evp_cipher_ctx_oiv(EVP_CIPHER_CTX* ctx)
{
#ifdef OPENSSL_1_1
    return (uint8_t*)EVP_CIPHER_CTX_original_iv(ctx);
#else
    return (uint8_t*)ctx->oiv;
#endif
}
