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

#include <modinfo.h>
#include <dcb.h>
#include <buffer.h>

MODULE_INFO info =
{
    MODULE_API_PROTOCOL,
    MODULE_IN_DEVELOPMENT,
    GWPROTOCOL_VERSION,
    "Test protocol"
};

static char *version_str = "V1.0.0";

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
    test_session      /**< Session                       */
};


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
GWPROTOCOL* GetModuleObject()
{
    return &MyObject;
}
