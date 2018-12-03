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
#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#if defined (NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <maxscale/alloc.h>
#include <maxscale/server.hh>
#include <maxscale/log.h>
#include <maxscale/paths.h>
#include <maxscale/config.hh>

// This is pretty ugly but it's required to test internal functions
#include "../config.cc"
#include "../server.cc"
#include "../internal/server.hh"

static mxs::ParamList params(
{
    {"address", "127.0.0.1"},
    {"port", "9876"},
    {"protocol", "HTTPD"},
    {"authenticator", "NullAuthAllow"}
}, config_server_params);

/**
 * test1    Allocate a server and do lots of other things
 *
 */
static int test1()
{
    SERVER* server;
    int result;
    char* status;

    /* Server tests */
    fprintf(stderr, "testserver : creating server called MyServer");
    server = server_alloc("uniquename", params.params());
    mxb_assert_message(server, "Allocating the server should not fail");

    char buf[120];
    fprintf(stderr, "\t..done\nTest Parameter for Server.");
    mxb_assert_message(!server_get_parameter(server, "name", buf, sizeof(buf)),
                       "Parameter should be null when not set");
    server_set_parameter(server, "name", "value");
    mxb_assert(server_get_parameter(server, "name", buf, sizeof(buf)));
    mxb_assert_message(strcmp("value", buf) == 0, "Parameter should be returned correctly");
    fprintf(stderr, "\t..done\nTesting Unique Name for Server.");
    mxb_assert_message(NULL == server_find_by_unique_name("non-existent"),
                       "Should not find non-existent unique name.");
    mxb_assert_message(server == server_find_by_unique_name("uniquename"), "Should find by unique name.");
    fprintf(stderr, "\t..done\nTesting Status Setting for Server.");
    status = server_status(server);
    mxb_assert_message(0 == strcmp("Running", status), "Status of Server should be Running by default.");
    if (NULL != status)
    {
        MXS_FREE(status);
    }
    server_set_status_nolock(server, SERVER_MASTER);
    status = server_status(server);
    mxb_assert_message(0 == strcmp("Master, Running", status), "Should find correct status.");
    server_clear_status_nolock(server, SERVER_MASTER);
    MXS_FREE(status);
    status = server_status(server);
    mxb_assert_message(0 == strcmp("Running", status),
                       "Status of Server should be Running after master status cleared.");
    if (NULL != status)
    {
        MXS_FREE(status);
    }
    fprintf(stderr, "\t..done\nRun Prints for Server and all Servers.");
    printServer(server);
    printAllServers();
    fprintf(stderr, "\t..done\nFreeing Server.");
    server_free((Server*)server);
    fprintf(stderr, "\t..done\n");
    return 0;
}

#define TEST(A, B) do {if (!(A)) {printf(B "\n"); return false;}} while (false)

bool test_load_config(const char* input, SERVER* server)
{
    DUPLICATE_CONTEXT dcontext;

    if (duplicate_context_init(&dcontext))
    {
        CONFIG_CONTEXT ccontext = {};
        ccontext.object = (char*)"";

        if (config_load_single_file(input, &dcontext, &ccontext))
        {
            CONFIG_CONTEXT* obj = ccontext.next;
            MXS_CONFIG_PARAMETER* param = obj->parameters;
            config_add_defaults(obj, config_server_params);

            TEST(strcmp(obj->object, server->name) == 0, "Server names differ");
            TEST(strcmp(server->address, config_get_param(param, "address")->value) == 0,
                 "Server addresses differ");
            TEST(strcmp(server->protocol, config_get_param(param, "protocol")->value) == 0,
                 "Server protocols differ");
            TEST(strcmp(server->authenticator, config_get_param(param, "authenticator")->value) == 0,
                 "Server authenticators differ");
            TEST(server->port == atoi(config_get_param(param, "port")->value), "Server ports differ");
            TEST(server_alloc(obj->object, obj->parameters), "Failed to create server from loaded config");
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
    char* persist_dir = MXS_STRDUP_A("./");
    set_config_persistdir(persist_dir);
    SERVER* server = server_alloc(name, params.params());
    TEST(server, "Server allocation failed");

    /** Make sure the files don't exist */
    unlink(config_name);
    unlink(old_config_name);

    /** Serialize server to disk */
    TEST(server_serialize(server), "Failed to synchronize original server");

    /** Load it again */
    TEST(test_load_config(config_name, server), "Failed to load the serialized server");

    /** We should have two identical servers */
    SERVER* created = server_find_by_unique_name(name);

    rename(config_name, old_config_name);

    /** Serialize the loaded server to disk */
    TEST(server_serialize(created), "Failed to synchronize the copied server");

    /** Check that they serialize to identical files */
    char cmd[1024];
    sprintf(cmd, "diff ./%s ./%s", config_name, old_config_name);
    TEST(system(cmd) == 0, "The files are not identical");

    return true;
}

int main(int argc, char** argv)
{
    /**
     * Prepare test environment by pre-loading modules. This prevents the server
     * allocation from failing if multiple modules from different directories are
     * loaded in one core function call.
     */
    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_STDOUT);
    set_libdir(MXS_STRDUP_A("../../modules/authenticator/NullAuthAllow/"));
    load_module("NullAuthAllow", MODULE_AUTHENTICATOR);
    set_libdir(MXS_STRDUP_A("../../modules/protocol/HTTPD/"));
    load_module("HTTPD", MODULE_PROTOCOL);

    int result = 0;

    result += test1();

    if (!test_serialize())
    {
        result++;
    }

    mxs_log_finish();
    exit(result);
}
