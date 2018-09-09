/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/cdefs.h>

#include <openssl/evp.h>

MXS_BEGIN_DECLS


EVP_CIPHER_CTX* mxs_evp_cipher_ctx_alloc();
void            mxs_evp_cipher_ctx_free(EVP_CIPHER_CTX* ctx);
uint8_t*        mxs_evp_cipher_ctx_buf(EVP_CIPHER_CTX* ctx);
uint8_t*        mxs_evp_cipher_ctx_oiv(EVP_CIPHER_CTX* ctx);

MXS_END_DECLS
