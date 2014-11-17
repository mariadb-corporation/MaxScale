#include <harness.h>
int main(int argc,char** argv)
{
	HARNESS_INSTANCE* inst;
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
