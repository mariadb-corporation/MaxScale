#include <harness.h>
int main()
{
	if(harness_init(argc,argv)){
		printf("Error: Initialization failed.\n");
		skygw_log_write(LOGFILE_ERROR,"Error: Initialization failed.\n");
		skygw_logmanager_done();
		skygw_logmanager_exit();
		return 1;
	}

	route_buffers();
	return 0;
}
