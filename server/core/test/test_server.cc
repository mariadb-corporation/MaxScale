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
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 08-10-2014   Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <maxscale/alloc.h>
#include <maxscale/server.h>
#include <maxscale/log_manager.h>
#include <maxscale/paths.h>

// This is pretty ugly but it's required to test internal functions
#include "../config.cc"
#include "../server.cc"

/**
 * test1    Allocate a server and do lots of other things
 *
  */
static int
test1()
{
    SERVER   *server;
    int     result;
    char    *status;

    /* Server tests */
    ss_dfprintf(stderr, "testserver : creating server called MyServer");
    set_libdir(MXS_STRDUP_A("../../modules/authenticator/NullAuthAllow/"));
    server = server_alloc("uniquename", "127.0.0.1", 9876, "HTTPD", "NullAuthAllow");
    ss_info_dassert(server, "Allocating the server should not fail");
    mxs_log_flush_sync();

    //ss_info_dassert(NULL != service, "New server with valid protocol and port must not be null");
    //ss_info_dassert(0 != service_isvalid(service), "Service must be valid after creation");

    char buf[120];
    ss_dfprintf(stderr, "\t..done\nTest Parameter for Server.");
    ss_info_dassert(!server_get_parameter(server, "name", buf, sizeof(buf)), "Parameter should be null when not set");
    server_add_parameter(server, "name", "value");
    mxs_log_flush_sync();
    ss_dassert(server_get_parameter(server, "name", buf, sizeof(buf)));
    ss_info_dassert(strcmp("value", buf) == 0, "Parameter should be returned correctly");
    ss_dfprintf(stderr, "\t..done\nTesting Unique Name for Server.");
    ss_info_dassert(NULL == server_find_by_unique_name("non-existent"),
                    "Should not find non-existent unique name.");
    mxs_log_flush_sync();
    ss_info_dassert(server == server_find_by_unique_name("uniquename"), "Should find by unique name.");
    ss_dfprintf(stderr, "\t..done\nTesting Status Setting for Server.");
    status = server_status(server);
    mxs_log_flush_sync();
    ss_info_dassert(0 == strcmp("Running", status), "Status of Server should be Running by default.");
    if (NULL != status)
    {
        MXS_FREE(status);
    }
    server_set_status_nolock(server, SERVER_MASTER);
    status = server_status(server);
    mxs_log_flush_sync();
    ss_info_dassert(0 == strcmp("Master, Running", status), "Should find correct status.");
    server_clear_status_nolock(server, SERVER_MASTER);
    MXS_FREE(status);
    status = server_status(server);
    mxs_log_flush_sync();
    ss_info_dassert(0 == strcmp("Running", status),
                    "Status of Server should be Running after master status cleared.");
    if (NULL != status)
    {
        MXS_FREE(status);
    }
    ss_dfprintf(stderr, "\t..done\nRun Prints for Server and all Servers.");
    printServer(server);
    printAllServers();
    mxs_log_flush_sync();
    ss_dfprintf(stderr, "\t..done\nFreeing Server.");
    ss_info_dassert(0 != server_free(server), "Free should succeed");
    ss_dfprintf(stderr, "\t..done\n");
    return 0;

}

#define TEST(A, B) do { if(!(A)){ printf(B"\n"); return false; }} while(false)

bool test_load_config(const char *input, SERVER *server)
{
    DUPLICATE_CONTEXT dcontext;

    if (duplicate_context_init(&dcontext))
    {
        CONFIG_CONTEXT ccontext = {};
        ccontext.object = (char*)"";

        if (config_load_single_file(input, &dcontext, &ccontext))
        {
            CONFIG_CONTEXT *obj = ccontext.next;
            MXS_CONFIG_PARAMETER *param = obj->parameters;

            TEST(strcmp(obj->object, server->name) == 0, "Server names differ");
            TEST(strcmp(server->address, config_get_param(param, "address")->value) == 0, "Server addresses differ");
            TEST(strcmp(server->protocol, config_get_param(param, "protocol")->value) == 0, "Server protocols differ");
            TEST(strcmp(server->authenticator, config_get_param(param, "authenticator")->value) == 0,
                 "Server authenticators differ");
            TEST(server->port == atoi(config_get_param(param, "port")->value), "Server ports differ");
            TEST(create_new_server(obj) == 0, "Failed to create server from loaded config");
            duplicate_context_finish(&dcontext);
            config_context_free(obj);
        }
    }

    return true;
}

bool test_serialize()
{
    char name[] = "serialized-server";
    char config_name[] = "serialized-server.cnf";
    char old_config_name[] = "serialized-server.cnf.old";
    char *persist_dir = MXS_STRDUP_A("./");
    set_config_persistdir(persist_dir);
    SERVER *server = server_alloc(name, "127.0.0.1", 9876, "HTTPD", "NullAuthAllow");
    TEST(server, "Server allocation failed");

    /** Make sure the files don't exist */
    unlink(config_name);
    unlink(old_config_name);

    /** Serialize server to disk */
    TEST(server_serialize(server), "Failed to synchronize original server");

    /** Load it again */
    TEST(test_load_config(config_name, server), "Failed to load the serialized server");

    /** We should have two identical servers */
    SERVER *created = server_find_by_unique_name(name);
    TEST(created->next == server, "We should end up with two servers");

    rename(config_name, old_config_name);

    /** Serialize the loaded server to disk */
    TEST(server_serialize(created), "Failed to synchronize the copied server");

    /** Check that they serialize to identical files */
    char cmd[1024];
    sprintf(cmd, "diff ./%s ./%s", config_name, old_config_name);
    TEST(system(cmd) == 0, "The files are not identical");

    return true;
}

int main(int argc, char **argv)
{
    int result = 0;

    result += test1();

    if (!test_serialize())
    {
        result++;
    }

    exit(result);
}
