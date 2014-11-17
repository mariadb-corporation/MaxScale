#include <my_config.h>
#include <mysql.h>
#include <harness.h>

int main(int argc,char** argv)
{

	static char* server_options[] = {
		"MariaDB Corporation MaxScale",
		"--datadir=./",
		"--language=./",
		"--skip-innodb",
		"--default-storage-engine=myisam",
		NULL
	};

	const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

	static char* server_groups[] = {
		"embedded",
		"server",
		"server",
		NULL
	};


	HARNESS_INSTANCE* inst;

	if(mysql_library_init(num_elements, server_options, server_groups)){
		printf("Embedded server init failed.\n");
		return 1;
	}


	if(harness_init(argc,argv,&inst) || inst->error){
		printf("Error: Initialization failed.\n");
		skygw_log_write(LOGFILE_ERROR,"Error: Initialization failed.\n");
		skygw_logmanager_done();
		skygw_logmanager_exit();
		return 1;
	}

	route_buffers();
	if(inst->expected){
		return compare_files(inst->outfile,inst->expected);
	}
	return 0;
}
