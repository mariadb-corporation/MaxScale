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
 * @file testprotocol.c - Testing protocol module
 *
 * Not intended for actual use. This protocol module does nothing useful and
 * is only meant to test that the module loading works.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 20/02/2015   Markus Mäkelä   Initial implementation
 *
 * @endverbatim
 */

#include <maxscale/modinfo.h>
#include <maxscale/dcb.h>
#include <maxscale/buffer.h>
#include <maxscale/gw_protocol.h>

 /* @see function load_module in load_utils.c for explanation of the following
  * lint directives.
 */
/*lint -e14 */
MODULE_INFO info =
{
    MODULE_API_PROTOCOL,
    MODULE_IN_DEVELOPMENT,
    GWPROTOCOL_VERSION,
    "Test protocol",
    "V1.1.0"
};
/*lint +e14 */

static int test_read(DCB* dcb){ return 1;}
static int test_write(DCB *dcb, GWBUF* buf){ return 1;}
static int test_write_ready(DCB *dcb){ return 1;}
static int test_error(DCB *dcb){ return 1;}
static int test_hangup(DCB *dcb){ return 1;}
static int test_accept(DCB *dcb){ return 1;}
static int test_connect(struct dcb *dcb, struct server *srv, struct session *ses){ return 1;}
static int test_close(DCB *dcb){ return 1;}
static int test_listen(DCB *dcb, char *config){ return 1;}
static int test_auth(DCB* dcb, struct server *srv, struct session *ses, GWBUF *buf){ return 1;}
static int test_session(DCB *dcb, void* data){ return 1;}
static char *test_default_auth(){return "NullAuthAllow";}
static int test_connection_limit(DCB *dcb, int limit){return 0;}
/**
 * The "module object" for the httpd protocol module.
 */
static GWPROTOCOL MyObject =
{
    test_read,        /**< Read - EPOLLIN handler        */
    test_write,       /**< Write - data from gateway     */
    test_write_ready, /**< WriteReady - EPOLLOUT handler */
    test_error,       /**< Error - EPOLLERR handler      */
    test_hangup,      /**< HangUp - EPOLLHUP handler     */
    test_accept,      /**< Accept                        */
    test_connect,     /**< Connect                       */
    test_close,       /**< Close                         */
    test_listen,      /**< Create a listener             */
    test_auth,        /**< Authentication                */
    test_session,     /**< Session                       */
    test_default_auth, /**< Default authenticator         */
    test_connection_limit   /**< Connection limit        */
};

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
GWPROTOCOL* GetModuleObject()
{
    return &MyObject;
}
/*lint +e14 */
