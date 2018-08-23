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
 * @file testbinlog.c - The MaxScale CHANGE MASTER TO syntax test
 *
 * Revision History
 *
 * Date     Who         Description
 * 24/08/2015   Massimiliano Pinto  Initial implementation
 *
 * @endverbatim
 */

#include "../blr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxbase/atomic.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/housekeeper.h>
#include <time.h>
#include <maxscale/log.h>
#include <maxscale/paths.h>
#include <maxscale/alloc.h>
#include <maxscale/utils.hh>
#include "../../../../core/internal/modules.h"
#include "../../../../core/internal/config.hh"

#include <maxscale/protocol/mysql.h>
#include <ini.h>
#include <sys/stat.h>
#include <getopt.h>

#include <maxscale/version.h>

// This isn't really a clean way of testing
#include "../../../../core/internal/service.hh"
#include <maxscale/config.hh>

static void printVersion(const char *progname);
static void printUsage(const char *progname);
static void master_free_parsed_options(CHANGE_MASTER_OPTIONS *options);
extern int blr_test_parse_change_master_command(char *input, char *error_string,
                                                CHANGE_MASTER_OPTIONS *config);
extern char *blr_test_set_master_logfile(ROUTER_INSTANCE *router, const char *filename, char *error);
extern int blr_test_handle_change_master(ROUTER_INSTANCE* router, char *command, char *error);

static struct option long_options[] =
{
    {"debug",     no_argument,            0,      'd'},
    {"verbose",   no_argument,            0,      'v'},
    {"version",   no_argument,            0,      'V'},
    {"fix",       no_argument,            0,      'f'},
    {"help",      no_argument,            0,      '?'},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    ROUTER_INSTANCE *inst;
    int ret;
    int rc;
    char error_string[BINLOG_ERROR_MSG_LEN + 1] = "";
    CHANGE_MASTER_OPTIONS change_master;
    char query[512 + 1] = "";
    char saved_query[512 + 1] = "";
    int command_offset = strlen("CHANGE MASTER TO");
    char *master_log_file = NULL;
    char *master_log_pos = NULL;
    SERVICE *service;
    int tests = 1;


    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);
    atexit(mxs_log_finish);

    mxs_log_set_priority_enabled(LOG_NOTICE, false);
    mxs_log_set_priority_enabled(LOG_ERR, true);
    mxs_log_set_priority_enabled(LOG_DEBUG, false);
    mxs_log_set_priority_enabled(LOG_INFO, false);

    set_libdir(MXS_STRDUP_A(".."));
    load_module("binlogrouter", MODULE_ROUTER);
    set_libdir(MXS_STRDUP_A("../../../protocol/MySQL/mariadbbackend/"));
    load_module("MySQLBackend", MODULE_PROTOCOL);
    set_libdir(MXS_STRDUP_A("../../../authenticator/MySQLBackendAuth/"));
    load_module("MySQLBackendAuth", MODULE_AUTHENTICATOR);
    set_libdir(MXS_STRDUP_A("../../../../../query_classifier/qc_sqlite/"));
    load_module("qc_sqlite", MODULE_QUERY_CLASSIFIER);

    qc_init(NULL, QC_SQL_MODE_DEFAULT, NULL, NULL);
    hkinit();

    CONFIG_CONTEXT ctx{(char*)""};
    config_add_defaults(&ctx, get_module("binlogrouter", MODULE_ROUTER)->parameters);

    const char* options = "server_id=3,heartbeat=200,binlogdir=/tmp/my_dir,"
        "transaction_safety=1,master_version=5.6.99-common,"
        "master_hostname=common_server,master_uuid=xxx-fff-cccc-fff,"
        "master_id=999,user=foo,password=bar";

    for (auto&& a : mxs::strtok(options, ","))
    {
        auto tmp = mxs::strtok(a, "=");
        config_replace_param(&ctx, tmp[0].c_str(), tmp[1].c_str());
    }

    config_replace_param(&ctx, "router_options", options);

    if ((service = service_alloc("test_service", "binlogrouter", ctx.parameters)) == NULL)
    {
        printf("Failed to allocate 'service' object\n");
        return 1;
    }

    config_parameter_free(ctx.parameters);

    // Declared in config.cc and needs to be removed if/when blr is refactored
    extern const MXS_MODULE_PARAM config_server_params[];

    mxs::ParamList p(
    {
        {"address", "_none_"},
        {"port", "3306"},
        {"protocol", "MySQLBackend"},
        {"authenticator", "MySQLBackendAuth"}
    }, config_server_params);

    SERVER* server = server_alloc("binlog_router_master_host", p.params());
    if (server == NULL)
    {
        printf("Failed to allocate 'server' object\n");
        return 1;
    }

    serviceAddBackend(service, server);

    inst = static_cast<ROUTER_INSTANCE*>(MXS_CALLOC(1, sizeof(ROUTER_INSTANCE)));
    if (inst == NULL)
    {
        return 1;
    }
    const char* user;
    const char* password;
    serviceGetUser(service, &user, &password);
    inst->service = service;
    inst->user = MXS_STRDUP_A(user);
    inst->password = MXS_STRDUP_A(password);

    MXS_NOTICE("testbinlog v1.0");

    if (inst->fileroot == NULL)
    {
        inst->fileroot = MXS_STRDUP_A(BINLOG_NAME_ROOT);
    }
    if (!inst->current_pos)
    {
        inst->current_pos = 4;
    }

    /********************************************
     *
     * First test suite is about syntax parsing
     *
     ********************************************/

    printf("--------- CHANGE MASTER TO parsing tests ---------\n");
    /**
     * Test 1: no given options
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO" + command_offset);
    /* Expected rc is 1, if 0 test fails */
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: no options for [%s] FAILED\n", tests, query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, no given options for [%s]\n", tests, query);
    }

    tests++;

    /**
     * Test 2: 1 wrong option without value
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO X" + command_offset);
    /* Expected rc is 1, if 0 test fails */
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: wrong options for [%s] FAILED\n", tests, query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, wrong options for [%s]\n", tests, query);
    }

    tests++;

    /**
     * Test 3: 1 wrong option with missing value after =
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO X=" + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);
    }

    tests++;

    /**
     * Test 4: 1 wrong option with missing value after =
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO X =" + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);
    }

    tests++;

    /**
     * Test 5: 1 wrong option with missing value after =
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO X= " + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);
    }

    tests++;

    /**
     * Test 6: 1 wrong option with missing value after =
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO X = " + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);
    }

    tests++;

    /**
     * Test 7: 1 valid option with missing value
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST" + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);
    }

    tests++;

    /**
     * Test 8: 1 valid option with missing value
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST = " + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);
    }

    tests++;
    master_free_parsed_options(&change_master);
    /**
     * Test 9: 1 valid option with value
     *
     * Expected rc is 0, if 1 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1'" + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 1)
    {
        printf("Test %d: valid option for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);
    }
    master_free_parsed_options(&change_master);
    tests++;

    /**
     * Test 10: 1 valid option and 2 invalid ones
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', Y, X" + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);
    }
    master_free_parsed_options(&change_master);
    tests++;

    /**
     * Test 11: 1 valid option and 1 with missing value
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=" + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);
    }
    master_free_parsed_options(&change_master);
    tests++;

    /**
     * Test 12: 2 valid options
     *
     * Expected rc is 0, if 1 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999" + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 1)
    {
        printf("Test %d: valid options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);
    }
    master_free_parsed_options(&change_master);
    tests++;

    /**
     * Test 13: 2 valid options  and 1 invalid
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWD='massi'" +
           command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);
    }
    master_free_parsed_options(&change_master);
    tests++;

    /**
     * Test 14: 3 valid options
     *
     * Expected rc is 0, if 1 test fails
     */
    strcpy(error_string, "");
    strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi'" +
           command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 1)
    {
        printf("Test %d: valid options [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);
    }
    master_free_parsed_options(&change_master);
    tests++;

    /**
     * Test 15: 5 valid options and 1 invalid
     *
     * Expected rc is 1, if 0 test fails
     */
    strcpy(error_string, "");
    strcpy(query,
           "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_fil=     'fffff', master_log_pos= 55"
           + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 0)
    {
        printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);
    }
    master_free_parsed_options(&change_master);
    tests++;

    /**
     * Test 16: 6 valid options
     *
     * Expected rc is 0, if 1 test fails
     */
    strcpy(error_string, "");
    memset(&change_master, 0, sizeof(change_master));
    strcpy(query,
           "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_file=     'fffff', master_log_pos= 55"
           + command_offset);
    strcpy(saved_query, query);
    rc = blr_test_parse_change_master_command(query, error_string, &change_master);
    if (rc == 1)
    {
        printf("Test %d: valid options [%s] FAILED\n", tests, saved_query);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);
    }

    change_master.host.clear();
    change_master.port.clear();
    change_master.user.clear();
    change_master.password.clear();
    change_master.binlog_pos.clear();
    tests++;

    printf("--------- MASTER_LOG_FILE tests ---------\n");

    /**
     * Test 17: use current binlog filename in master_state != BLRM_UNCONFIGURED
     * and try to set a new filename with wrong format from previous test
     *
     * Expected master_log_file is NULL and error_string is not empty
     */
    inst->master_state = BLRM_SLAVE_STOPPED;
    strcpy(error_string, "");

    master_log_file = blr_test_set_master_logfile(inst, change_master.binlog_file.c_str(), error_string);

    if (master_log_file == NULL)
    {
        if (strlen(error_string))
        {
            printf("Test %d PASSED, MASTER_LOG_FILE [%s]: [%s]\n", tests,
                   change_master.binlog_file.c_str(), error_string);
        }
        else
        {
            printf("Test %d: set MASTER_LOG_FILE [%s] FAILED, an error message was expected\n", tests,
                   change_master.binlog_file.c_str());
            return 1;
        }
    }
    else
    {
        printf("Test %d: set MASTER_LOG_FILE [%s] FAILED, "
               "NULL was expected from blr_test_set_master_logfile()\n",
               tests, change_master.binlog_file.c_str());
        return 1;
    }

    tests++;
    change_master.binlog_file.clear();
    printf("--- MASTER_LOG_POS and MASTER_LOG_FILE rule/constraints checks ---\n");

    /********************************************
     *
     * Second part of test suite is for checking
     * rules and constraints once syntax is OK
     *
     ********************************************/

    /**
     * Test 18: change master without MASTER_LOG_FILE in BLRM_UNCONFIGURED state
     *
     * Expected rc = -1 and master_state still set to BLRM_UNCONFIGURED
     */
    inst->master_state = BLRM_UNCONFIGURED;
    strcpy(error_string, "");
    strcpy(query,
           "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_pos= 55");

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc == -1 && inst->master_state == BLRM_UNCONFIGURED)
    {
        printf("Test %d PASSED, in BLRM_UNCONFIGURED state. Message [%s]\n", tests, error_string);
    }
    else
    {
        printf("Test %d: an error message was expected from blr_test_handle_change_master(), Master State is %s. Message [%s]\n",
               tests, blrm_states[inst->master_state], error_string);
        return 1;
    }

    tests++;

    /**
     * Test 19: use selected binlog filename in BLRM_UNCONFIGURED state
     *
     * Expected rc = -1 and master_state still set to BLRM_UNCONFIGURED
     */

    inst->master_state = BLRM_UNCONFIGURED;
    strcpy(error_string, "");
    strcpy(query,
           "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_file=     'file.000053', master_log_pos= 1855");

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc == -1 && inst->master_state == BLRM_UNCONFIGURED)
    {
        printf("Test %d PASSED, cannot set MASTER_LOG_FILE in BLRM_UNCONFIGURED state for [%s]. Message [%s]\n",
               tests, query, error_string);
    }
    else
    {
        printf("Test %d: set MASTER_LOG_FILE in BLRM_UNCONFIGURED state FAILED, an error message was expected from blr_test_handle_change_master(), Master State is %s. Message [%s]\n",
               tests, blrm_states[inst->master_state], error_string);
        return 1;
    }

    tests++;

    /**
     * Test 20: use selected binlog filename and pos in state != BLRM_UNCONFIGURED
     * Current binlog and pos are same specified in the change master command
     *
     * Expected rc = 0
     */
    inst->master_state = BLRM_UNCONNECTED;
    strcpy(error_string, "");
    strncpy(inst->binlog_name, "file-bin.00008", BINLOG_FNAMELEN);
    inst->current_pos = 55;
    strcpy(query,
           "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_file=     'file-bin.00008', master_log_pos= 55");

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc == 0)
    {
        printf("Test %d PASSED, set MASTER_LOG_FILE and MASTER_LOG_POS for [%s]\n", tests, query);
    }
    else
    {
        printf("Test %d: set MASTER_LOG_FILE and MASTER_LOG_POS FAILED, Master State is %s. Message [%s]\n", tests,
               blrm_states[inst->master_state], error_string);
        return 1;
    }

    tests++;

    /**
     * Test 21: use selected binlog filename and pos in state != BLRM_UNCONFIGURED
     * Current binlog is not the one specified in the change master command
     *
     * Expected rc = -1
     */

    strncpy(inst->binlog_name, "file.000006", BINLOG_FNAMELEN);
    inst->current_pos = 10348;
    strcpy(inst->fileroot, "file");
    strcpy(error_string, "");
    inst->master_state = BLRM_UNCONNECTED;
    strcpy(query,
           "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', MASTER_LOG_file ='mmmm.098777', master_log_pos= 55");

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc == -1)
    {
        printf("Test %d PASSED, cannot set MASTER_LOG_FILE for [%s], Message [%s]\n", tests, query, error_string);
    }
    else
    {
        printf("Test %d: set MASTER_LOG_FILE, Master State is %s Failed, Message [%s]\n", tests,
               blrm_states[inst->master_state], error_string);
        return 1;
    }

    tests++;

    /**
     * Test 22: use selected binlog filename is next one in sequence and specified pos is 4
     * in any state
     *
     * Expected rc >= 0
     */
    strcpy(error_string, "");
    strncpy(inst->binlog_name, "file.100506", BINLOG_FNAMELEN);
    inst->current_pos = 1348;
    strcpy(inst->fileroot, "file");
    strcpy(query, "CHANGE MASTER TO master_log_pos= 4 , master_log_file='file.100507'");

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc >= 0)
    {
        printf("Test %d PASSED, set MASTER_LOG_FILE for [%s]\n", tests, query);
    }
    else
    {
        printf("Test %d: set MASTER_LOG_FILE FAILED, Master State is %s. Message [%s]\n", tests,
               blrm_states[inst->master_state], error_string);
        return 1;
    }

    tests++;

    /**
     * Test 23: use selected pos that's not the current one
     * in state != BLRM_UNCONFIGURED
     *
     * Expected rc = -1
     */
    inst->master_state = BLRM_UNCONNECTED;
    strcpy(error_string, "");
    strncpy(inst->binlog_name, "file.100506", BINLOG_FNAMELEN);
    inst->current_pos = 138;
    strcpy(inst->fileroot, "file");
    strcpy(query, "CHANGE MASTER TO master_log_pos= 49  ");

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc == -1)
    {
        printf("Test %d PASSED, cannot set MASTER_LOG_POS for [%s], Message [%s]\n", tests, query, error_string);
    }
    else
    {
        printf("Test %d: set MASTER_LOG_POS FAILED, Master State is %s. Message [%s]\n", tests,
               blrm_states[inst->master_state], error_string);
        return 1;
    }

    tests++;

    /**
     * MASTER_USE_GTID tests
     */

    /**
     * Test 24: use MASTER_LOG_FILE without MASTER_USE_GTID=Slave_pos
     * in state != BLRM_UNCONFIGURED
     *
     * Expected rc = -1
     */
    inst->master_state = BLRM_UNCONNECTED;
    inst->mariadb10_compat = 1;
    inst->mariadb10_gtid = 1;
    inst->mariadb10_master_gtid = 1;

    strcpy(error_string, "");
    strncpy(inst->binlog_name, "file.100506", BINLOG_FNAMELEN);
    inst->current_pos = 138;
    strcpy(inst->fileroot, "file");
    sprintf(query,
            "CHANGE MASTER TO MASTER_LOG_POS=1991, "
            "MASTER_LOG_FILE='%s'",
            inst->binlog_name);

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc == -1)
    {
        printf("Test %d PASSED, GTID cannot set MASTER_LOG_FILE "
               "for [%s], Message [%s]\n",
               tests,
               query,
               error_string);
    }
    else
    {
        printf("Test %d: GTID set MASTER_LOG_FILE FAILED, "
               "Master State is %s. Message [%s]\n",
               tests,
               blrm_states[inst->master_state], error_string);
        return 1;
    }

    tests++;

    /**
     * Test 25: use MASTER_USE_GTID=Slave_pos with MASTER_LOG_FILE=''
     * and MASTER_LOG_POS = 9999
     * in state != BLRM_UNCONFIGURED
     *
     * Expected rc = -1
     */
    inst->master_state = BLRM_UNCONNECTED;
    inst->mariadb10_compat = 1;
    inst->mariadb10_gtid = 1;
    inst->mariadb10_master_gtid = 1;
    strcpy(error_string, "");
    strncpy(inst->binlog_name, "file.100506", BINLOG_FNAMELEN);
    inst->current_pos = 328;
    strcpy(inst->fileroot, "file");
    strcpy(query,
           "CHANGE MASTER TO MASTER_LOG_POS=1991, MASTER_USE_GTID=Slave_pos, "
           "MASTER_LOG_FILE=''");

    rc = blr_test_handle_change_master(inst, query, error_string);

    if (rc == -1)
    {
        printf("Test %d: GTID set MASTER_USE_GTID=Slave_pos FAILED, "
               "Master State is %s. Message [%s]\n",
               tests,
               blrm_states[inst->master_state], error_string);
        return 1;
    }
    else
    {
        printf("Test %d PASSED, GTID set MASTER_USE_GTID=Slave_pos "
               "for [%s]: %s\n",
               tests,
               query, error_string);
    }

    tests++;

    /**
     * Verify SQL query initial comment skipping function works on a real use case.
     */
    const char *mysql_connector_j_actual =
        blr_skip_leading_sql_comments("/* mysql-connector-java-5.1.39 ( Revision: 3289a357af6d09ecc1a10fd3c26e95183e5790ad ) */SELECT  @@session.auto_increment_increment AS auto_increment_increment, @@character_set_client AS character_set_client, @@character_set_connection AS character_set_connection, @@character_set_results AS character_set_results, @@character_set_server AS character_set_server, @@init_connect AS init_connect, @@interactive_timeout AS interactive_timeout, @@license AS license, @@lower_case_table_names AS lower_case_table_names, @@max_allowed_packet AS max_allowed_packet, @@net_buffer_length AS net_buffer_length, @@net_write_timeout AS net_write_timeout, @@query_cache_size AS query_cache_size, @@query_cache_type AS query_cache_type, @@sql_mode AS sql_mode, @@system_time_zone AS system_time_zone, @@time_zone AS time_zone, @@tx_isolation AS tx_isolation, @@wait_timeout AS wait_timeout");
    const char *mysql_connector_j_expected =
        "SELECT  @@session.auto_increment_increment AS auto_increment_increment, @@character_set_client AS character_set_client, @@character_set_connection AS character_set_connection, @@character_set_results AS character_set_results, @@character_set_server AS character_set_server, @@init_connect AS init_connect, @@interactive_timeout AS interactive_timeout, @@license AS license, @@lower_case_table_names AS lower_case_table_names, @@max_allowed_packet AS max_allowed_packet, @@net_buffer_length AS net_buffer_length, @@net_write_timeout AS net_write_timeout, @@query_cache_size AS query_cache_size, @@query_cache_type AS query_cache_type, @@sql_mode AS sql_mode, @@system_time_zone AS system_time_zone, @@time_zone AS time_zone, @@tx_isolation AS tx_isolation, @@wait_timeout AS wait_timeout";
    if (strcmp(mysql_connector_j_actual, mysql_connector_j_expected) == 0)
    {
        printf("Test %d PASSED\n", tests);
    }
    else
    {
        printf("Test %d FAILED: Actual result: %s\n", tests, mysql_connector_j_actual);
        return 1;
    }

    tests++;

    const char *no_comment_query_actual = blr_skip_leading_sql_comments("SELECT foo FROM bar LIMIT 1");
    const char *no_comment_query_expected = "SELECT foo FROM bar LIMIT 1";
    if (strcmp(no_comment_query_actual, no_comment_query_expected) == 0)
    {
        printf("Test %d PASSED\n", tests);
    }
    else
    {
        printf("Test %d FAILED: Actual result: %s\n", tests, no_comment_query_actual);
        return 1;
    }

    tests++;

    const char *unclosed_comment_query_actual = blr_skip_leading_sql_comments("/* SELECT foo FROM bar LIMIT 1");
    const char *unclosed_comment_query_expected = "";
    if (strcmp(unclosed_comment_query_actual, unclosed_comment_query_expected) == 0)
    {
        printf("Test %d PASSED\n", tests);
    }
    else
    {
        printf("Test %d FAILED: Actual result: %s\n", tests, no_comment_query_actual);
        return 1;
    }

    MXS_FREE(inst->user);
    MXS_FREE(inst->password);
    MXS_FREE(inst->fileroot);
    MXS_FREE(inst);
    return 0;
}

static void
master_free_parsed_options(CHANGE_MASTER_OPTIONS *options)
{
    options->host.clear();
    options->port.clear();
    options->user.clear();
    options->password.clear();
    options->binlog_file.clear();
    options->binlog_pos.clear();
}
