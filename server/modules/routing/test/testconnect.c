#include <my_config.h>
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

int main(int argc, char** argv)
{

	MYSQL* server;
	char *host = NULL, *str_baseline = NULL,*str_test = NULL, *errmsg = NULL;
	unsigned int port;
	int rval, iterations,i;
	clock_t begin,end;
	size_t offset;
	struct timeval real_begin,real_end,real_baseline,real_test;
	time_t time;
	double baseline, test, ratio, result;

	if(argc < 7){
		fprintf(stderr,"Usage: %s <iterations> <baseline host> <baseline port> <test host> <test port> <max result ratio>\n",argv[0]);
		fprintf(stderr,"The ratio is measured as:\ntest time / baseline time\n");
		fprintf(stderr,"The test fails if this ratio is exceeded.\n");
		return 1;
	}


	;

	if((str_baseline = calloc(256,sizeof(char))) == NULL){
		return 1;
	}

	if((str_test = calloc(256,sizeof(char))) == NULL){
		free(str_baseline);
		return 1;
	}

	iterations = atoi(argv[1]);
	host = strdup(argv[2]);
	port = atoi(argv[3]);
	ratio = atof(argv[6]);
	rval = 0;

	if(ratio <= 0.0){
		return 1;
	}
	
	/**Testing direct connection to master*/

	printf("Connecting to MySQL server through %s:%d.\n",host,port);
	gettimeofday(&real_begin,NULL);
	begin = clock();

	if((server = mysql_init(NULL)) == NULL){
		return 1;
	}

	if(mysql_real_connect(server,host,"maxuser","maxpwd",NULL,port,NULL,0) == NULL){
		rval = 1;
		printf( "Failed to connect to database: Error: %s\n",
				mysql_error(server));
		goto report;
	}

	for(i = 0;i<iterations;i++)
		{
			if(mysql_change_user(server,"maxuser","maxpwd",NULL)){
				rval = 1;
				printf( "Failed to change user: Error: %s\n",
						mysql_error(server));
				goto report;
			}
		}

	mysql_close(server);			

	end = clock();
	gettimeofday(&real_end,NULL);
	baseline = (double)(end - begin)/CLOCKS_PER_SEC;
	timersub(&real_end,&real_begin,&real_baseline);

	free(host);
	host = strdup(argv[4]);
	port = atoi(argv[5]);

	/**Testing connection to master through MaxScale*/

	printf("Connecting to MySQL server through %s:%d.\n",host,port);
	gettimeofday(&real_begin,NULL);
	begin = clock();

	if((server = mysql_init(NULL)) == NULL){
		return 1;
	}

	if(mysql_real_connect(server,host,"maxuser","maxpwd",NULL,port,NULL,0) == NULL){
		rval = 1;
		printf("Failed to connect to database: Error: %s\n",
				mysql_error(server));
		goto report;
	}

	for(i = 0;i<iterations;i++)
		{
			if(mysql_change_user(server,"maxuser","maxpwd",NULL)){
				rval = 1;
				printf("Failed to change user: Error: %s\n",
						mysql_error(server));
				goto report;
			}
		}

	mysql_close(server);			

	end = clock();
	gettimeofday(&real_end,NULL);

	test = (double)(end - begin)/CLOCKS_PER_SEC;
	timersub(&real_end,&real_begin,&real_test);

	report:

	if(rval){	

		printf("\nTest failed: Errors during test run.\n");

	}else{
	
		struct tm *tm;
		time = real_baseline.tv_sec;
		tm = localtime(&time);
		offset = strftime(str_baseline,256*sizeof(char),"%S",tm);
		sprintf(str_baseline + offset,".%06d",(int)real_baseline.tv_usec);
		time = real_test.tv_sec;
		tm = localtime(&time);
		offset = strftime(str_test,256*sizeof(char),"%S",tm);
		sprintf(str_test + offset,".%06d",(int)real_test.tv_usec); 

		printf("\n\tCPU time in seconds\n\nDirect connection: %f\nThrough MaxScale: %f\n",baseline,test);
		printf("\n\tReal time in seconds\n\nDirect connection: %s\nThrough MaxScale: %s\n",str_baseline,str_test);

		double base_res = real_baseline.tv_sec + (real_baseline.tv_usec / 1000000.0);
		double test_res = real_test.tv_sec + (real_test.tv_usec / 1000000.0);
		result = test_res/base_res;

        printf("\nTest passed: Time ratio was %f.\n",result);
	}
	free(str_baseline);
	free(str_test);
	free(host);
	free(errmsg);
	return rval;
}
