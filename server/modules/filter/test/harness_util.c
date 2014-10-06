#include <harness.h>
int main(int argc,char** argv)
{
	if(harness_init(argc,argv) || instance.error){
		printf("Error: Initialization failed.\n");
		skygw_log_write(LOGFILE_ERROR,"Error: Initialization failed.\n");
		skygw_logmanager_done();
		skygw_logmanager_exit();
		return 1;
	}

	route_buffers();
	return 0;
}
