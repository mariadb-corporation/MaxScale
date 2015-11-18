/*
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
 * Copyright MariaDB Corporation Ab 2015
 */

/**
 * @file testbinlog.c - The MaxScale CHANGE MASTER TO syntax test
 *
 * Revision History
 *
 * Date		Who			Description
 * 24/08/2015	Massimiliano Pinto	Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <atomic.h>
#include <spinlock.h>
#include <blr.h>
#include <dcb.h>
#include <spinlock.h>
#include <housekeeper.h>
#include <time.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <gwdirs.h>

#include <mysql_client_server_protocol.h>
#include <ini.h>
#include <sys/stat.h>
#include <getopt.h>

#include <version.h>

static void printVersion(const char *progname);
static void printUsage(const char *progname);
extern int blr_test_parse_change_master_command(char *input, char *error_string, CHANGE_MASTER_OPTIONS *config);
extern char *blr_test_set_master_logfile(ROUTER_INSTANCE *router, char *filename, char *error);
extern int blr_test_handle_change_master(ROUTER_INSTANCE* router, char *command, char *error);

int
MaxScaleUptime()
{
return 1;
}

static struct option long_options[] = {
  {"debug",     no_argument,            0,      'd'},
  {"verbose",   no_argument,            0,      'v'},
  {"version",   no_argument,            0,      'V'},
  {"fix",       no_argument,            0,      'f'},
  {"help",      no_argument,            0,      '?'},
  {0, 0, 0, 0}
};

int main(int argc, char **argv) {
        ROUTER_INSTANCE *inst;
        int ret;
	int rc;
	char error_string[BINLOG_ERROR_MSG_LEN + 1] = "";
	CHANGE_MASTER_OPTIONS change_master;
	char query[255+1]="";
	char saved_query[255+1]="";
	int command_offset = strlen("CHANGE MASTER TO");
	char *master_log_file = NULL;
	char *master_log_pos = NULL;
	SERVICE	*service;
	char *roptions;
	int tests = 1;

	roptions = strdup("server-id=3,heartbeat=200,binlogdir=/not_exists/my_dir,transaction_safety=1,master_version=5.6.99-common,master_hostname=common_server,master_uuid=xxx-fff-cccc-fff,master-id=999");

	mxs_log_init(NULL, NULL, LOG_TARGET_DEFAULT);

	mxs_log_set_priority_enabled(LOG_DEBUG, false);
	mxs_log_set_priority_enabled(LOG_INFO, false);
	mxs_log_set_priority_enabled(LOG_NOTICE, false);
	mxs_log_set_priority_enabled(LOG_ERR, false);

	service = service_alloc("test_service", "binlogrouter");
	service->credentials.name = strdup("foo");
	service->credentials.authdata = strdup("bar");

	{
		char *lasts;
		SERVER *server;
		char *s = strtok_r(roptions, ",", &lasts);
		while (s)
		{
			serviceAddRouterOption(service, s);
			s = strtok_r(NULL, ",", &lasts);
		}

		server = server_alloc("_none_", "MySQLBackend", (int)3306);
		if (server == NULL) {
			if (service->users) {
				users_free(service->users);
			}

			return 1;
		}

		server_set_unique_name(server, "binlog_router_master_host");
		serviceAddBackend(service, server);
	}

	if ((inst = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL) {
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
			"Error: Memory allocation FAILED for ROUTER_INSTANCE")));

		mxs_log_flush_sync();
		mxs_log_finish();

		return 1;
	}

	inst->service = service;
	inst->user = service->credentials.name;
	inst->password = service->credentials.authdata;

	LOGIF(LM, (skygw_log_write_flush(LOGFILE_MESSAGE, "testbinlog v1.0")));

	if (inst->fileroot == NULL)
		inst->fileroot = strdup(BINLOG_NAME_ROOT);
	if (!inst->current_pos)
		inst->current_pos = 4;

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
	if (rc == 0) {
		printf("Test %d: no options for [%s] FAILED\n", tests, query);
		return 1;
	} else printf("Test %d PASSED, no given options for [%s]\n", tests, query);

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
	if (rc == 0) {
		printf("Test %d: wrong options for [%s] FAILED\n", tests, query);
		return 1;
	} else printf("Test %d PASSED, wrong options for [%s]\n", tests, query);

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
	if (rc == 0) {
		printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);

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
	if (rc == 0) {
		printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);

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
	if (rc == 0) {
		printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);

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
	if (rc == 0) {
		printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);

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
	if (rc == 0) {
		printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);

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
	if (rc == 0) {
		printf("Test %d: wrong options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, wrong options for [%s]\n", tests, saved_query);

	tests++;

	/**
	 * Test 9: 1 valid option with value
	 *
	 * Expected rc is 0, if 1 test fails
	 */
	strcpy(error_string, "");
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1'" + command_offset);
	strcpy(saved_query, query);
	rc = blr_test_parse_change_master_command(query, error_string, &change_master);
	if (rc == 1) {
		printf("Test %d: valid option for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);

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
	if (rc == 0) {
		printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);

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
	if (rc == 0) {
		printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);

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
	if (rc == 1) {
		printf("Test %d: valid options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);

	tests++;

	/**
	 * Test 13: 2 valid options  and 1 invalid
	 *
	 * Expected rc is 1, if 0 test fails
	 */
	strcpy(error_string, "");
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWD='massi'" + command_offset);
	strcpy(saved_query, query);
	rc = blr_test_parse_change_master_command(query, error_string, &change_master);
	if (rc == 0) {
		printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);

	tests++;

	/**
	 * Test 14: 3 valid options
	 *
	 * Expected rc is 0, if 1 test fails
	 */
	strcpy(error_string, "");
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi'" + command_offset);
	strcpy(saved_query, query);
	rc = blr_test_parse_change_master_command(query, error_string, &change_master);
	if (rc == 1) {
		printf("Test %d: valid options [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);

	tests++;

	/**
	 * Test 15: 5 valid options and 1 invalid
	 *
	 * Expected rc is 1, if 0 test fails
	 */
	strcpy(error_string, "");
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_fil=     'fffff', master_log_pos= 55" + command_offset);
	strcpy(saved_query, query);
	rc = blr_test_parse_change_master_command(query, error_string, &change_master);
	if (rc == 0) {
		printf("Test %d: valid / not valid options for [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid / not valid options for [%s]\n", tests, saved_query);

	tests++;

	/**
	 * Test 16: 6 valid options
	 *
	 * Expected rc is 0, if 1 test fails
	 */
	strcpy(error_string, "");
	memset(&change_master, 0, sizeof(change_master));
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_file=     'fffff', master_log_pos= 55" + command_offset);
	strcpy(saved_query, query);
	rc = blr_test_parse_change_master_command(query, error_string, &change_master);
	if (rc == 1) {
		printf("Test %d: valid options [%s] FAILED\n", tests, saved_query);
		return 1;
	} else printf("Test %d PASSED, valid options for [%s]\n", tests, saved_query);

	tests++;

	printf("--------- MASTER_LOG_FILE tests ---------\n");

	/**
 	 * Test 17: use current binlog filename in master_state != BLRM_UNCONFIGURED
	 * and try to set a new filename with wront format from previous test
	 *
	 * Expected master_log_file is NULL and error_string is not empty
	 */
	inst->master_state = BLRM_SLAVE_STOPPED;
	strcpy(error_string, "");

	master_log_file = blr_test_set_master_logfile(inst, change_master.binlog_file, error_string);

	if (master_log_file == NULL) {
		if (strlen(error_string)) {
			printf("Test %d PASSED, MASTER_LOG_FILE [%s]: [%s]\n", tests, change_master.binlog_file, error_string);
		} else {
			printf("Test %d: set MASTER_LOG_FILE [%s] FAILED, an error message was expected\n", tests, change_master.binlog_file);
			return 1;
		}
	} else {
		printf("Test %d: set MASTER_LOG_FILE [%s] FAILED, NULL was expected from blr_test_set_master_logfile()\n", tests, change_master.binlog_file);
		return 1;
	}

	tests++;

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
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_pos= 55");

	rc = blr_test_handle_change_master(inst, query, error_string);

	if (rc == -1 && inst->master_state == BLRM_UNCONFIGURED) {
		printf("Test %d PASSED, in BLRM_UNCONFIGURED state. Message [%s]\n", tests, error_string);
	} else {
		printf("Test %d: an error message was expected from blr_test_handle_change_master(), Master State is %s. Message [%s]\n", tests, blrm_states[inst->master_state], error_string);
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
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_file=     'file.000053', master_log_pos= 1855");

	rc = blr_test_handle_change_master(inst, query, error_string);

	if (rc == -1 && inst->master_state == BLRM_UNCONFIGURED) {
		printf("Test %d PASSED, cannot set MASTER_LOG_FILE in BLRM_UNCONFIGURED state for [%s]. Message [%s]\n", tests, query, error_string);
	} else {
		printf("Test %d: set MASTER_LOG_FILE in BLRM_UNCONFIGURED state FAILED, an error message was expected from blr_test_handle_change_master(), Master State is %s. Message [%s]\n", tests, blrm_states[inst->master_state], error_string);
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
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', master_log_file=     'file-bin.00008', master_log_pos= 55");

	rc = blr_test_handle_change_master(inst, query, error_string);

	if (rc == 0) {
		printf("Test %d PASSED, set MASTER_LOG_FILE and MASTER_LOG_POS for [%s]\n", tests, query);
	} else {
		printf("Test %d: set MASTER_LOG_FILE and MASTER_LOG_POS FAILED, Master State is %s. Message [%s]\n", tests, blrm_states[inst->master_state], error_string);
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
	strcpy(query, "CHANGE MASTER TO MasTER_hoST =  '127.0.0.1', MASTER_PORT=9999, MASTER_PASSWORD='massi', MAster_user='eee', MASTER_LOG_file ='mmmm.098777', master_log_pos= 55");

	rc = blr_test_handle_change_master(inst, query, error_string);

	if (rc == -1) {
		printf("Test %d PASSED, cannot set MASTER_LOG_FILE for [%s], Message [%s]\n", tests, query, error_string);
	} else {
		printf("Test %d: set MASTER_LOG_FILE, Master State is %s Failed, Message [%s]\n", tests, blrm_states[inst->master_state], error_string);
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

	if (rc >= 0) {
		printf("Test %d PASSED, set MASTER_LOG_FILE for [%s]\n", tests, query);
	} else {
		printf("Test %d: set MASTER_LOG_FILE FAILED, Master State is %s. Message [%s]\n", tests, blrm_states[inst->master_state], error_string);
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

	if (rc == -1) {
		printf("Test %d PASSED, cannot set MASTER_LOG_POS for [%s], Message [%s]\n", tests, query, error_string);
	} else {
		printf("Test %d: set MASTER_LOG_POS FAILED, Master State is %s. Message [%s]\n", tests, blrm_states[inst->master_state], error_string);
		return 1;
	}

	mxs_log_flush_sync();
	mxs_log_finish();

	free(inst);

	return 0;
}
