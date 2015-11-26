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
 * @file maxbinlogcheck.c - The MaxScale binlog check utility
 *
 * This utility checks a MySQL 5.6 and MariaDB 10.0.X binlog file and reports
 * any found error or an incomplete transaction.
 * It suggests the pos the file should be trucatetd at.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 24/07/2015	Massimiliano Pinto	Initial implementation
 * 26/08/2015	Massimiliano Pinto	Added mariadb10 option
 *					for MariaDB 10 binlog compatibility
 *					Currently MariadDB 10 starting transactions
 *					are detected checking GTID event
 *					with flags = 0
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

#include <mysql_client_server_protocol.h>
#include <ini.h>
#include <sys/stat.h>
#include <getopt.h>

#include <version.h>
#include <gwdirs.h>

extern int blr_read_events_all_events(ROUTER_INSTANCE *router, int fix, int debug);
extern uint32_t extract_field(uint8_t *src, int bits);
static void printVersion(const char *progname);
static void printUsage(const char *progname);

static struct option long_options[] = {
  {"debug",	no_argument,		0,	'd'},
  {"version",	no_argument,		0,	'V'},
  {"fix",	no_argument,		0,	'f'},
  {"mariadb10",	no_argument,		0,	'M'},
  {"help",	no_argument,		0,	'?'},
  {0, 0, 0, 0}
};

char *binlog_check_version = "1.1.0";

int
MaxScaleUptime()
{
return 1;
}

int main(int argc, char **argv) {
	ROUTER_INSTANCE *inst;
	int fd;
	int ret;
	char *ptr;
	char path[PATH_MAX+1] = "";
	unsigned long   filelen = 0;
	struct  stat    statb;
	char	c;
	int	option_index = 0;
	int	num_args = 0;
	int	debug_out = 0;
	int	fix_file = 0;
	int	mariadb10_compat = 0;

	while ((c = getopt_long(argc, argv, "dVfM?", long_options, &option_index)) >= 0)
	{
		switch (c) {
			case 'd':
				debug_out = 1;
				break;
			case 'V':
				printVersion(*argv);
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				fix_file = 1;
				break;
			case 'M':
				mariadb10_compat = 1;
				break;
			case '?':
				printUsage(*argv);
				exit(optopt ? EXIT_FAILURE : EXIT_SUCCESS);
		}      
	}

	num_args = optind;

	mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);
	mxs_log_set_augmentation(0);
	mxs_log_set_priority_enabled(LOG_DEBUG, debug_out);

	if ((inst = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL) {
		MXS_ERROR("Memory allocation failed for ROUTER_INSTANCE");

		mxs_log_flush_sync();
      		mxs_log_finish();

		return 1;
	}

	if (argv[num_args] == NULL) {
		printf("ERROR: No binlog file was specified\n");
		exit(EXIT_FAILURE);
	}

	strncpy(path, argv[num_args], PATH_MAX);

	if (fix_file)
		fd = open(path, O_RDWR, 0666);
	else
		fd = open(path, O_RDONLY, 0666);

	if (fd == -1)
	{
		MXS_ERROR("Failed to open binlog file %s: %s",
                          path, strerror(errno));
        
		mxs_log_flush_sync();
		mxs_log_finish();

		free(inst);

		return 1;
	}

	inst->binlog_fd = fd;

	if (mariadb10_compat == 1)
		inst->mariadb10_compat = 1;

	ptr = strrchr(path, '/');
	if (ptr)
		strncpy(inst->binlog_name, ptr+1, BINLOG_FNAMELEN);
	else
		strncpy(inst->binlog_name, path, BINLOG_FNAMELEN);

	MXS_NOTICE("maxbinlogcheck %s", binlog_check_version);

	if (fstat(inst->binlog_fd, &statb) == 0)
		filelen = statb.st_size;

	MXS_NOTICE("Checking %s (%s), size %lu bytes", path, inst->binlog_name, filelen);

	/* read binary log */
	ret = blr_read_events_all_events(inst, fix_file, debug_out);

	close(inst->binlog_fd);

	mxs_log_flush_sync();

	MXS_NOTICE("Check retcode: %i, Binlog Pos = %lu", ret, inst->binlog_position);

	mxs_log_flush_sync();
	mxs_log_finish();

	free(inst);

	return 0;
}

/**
 * Print version information
 */
static void
printVersion(const char *progname)
{
	printf("%s Version %s\n", progname, binlog_check_version);
}

/**
 * Display the --help text.
 */
static void
printUsage(const char *progname)
{
	printVersion(progname);

	printf("The MaxScale binlog check utility.\n\n");
	printf("Usage: %s [-f] [-d] [-v] [<binlog file>]\n\n", progname);
	printf("  -f|--fix		Fix binlog file, require write permissions (truncate)\n");
	printf("  -d|--debug		Print debug messages\n");
	printf("  -M|--mariadb10	MariaDB 10 binlog compatibility\n");
	printf("  -V|--version          print version information and exit\n");
	printf("  -?|--help             Print this help text\n");
}

