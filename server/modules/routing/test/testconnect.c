#include <my_config.h>
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int main(int argc, char** argv)
{

	MYSQL* server;
	char *host;
	unsigned int port;
	int rval, iterations,i;
	clock_t begin,end;
	double baseline,test;

	if(argc < 6){
		fprintf(stderr,"Usage: %s <iterations> <baseline host> <baseline port> <test host> <test port>\n",argv[0]);
		return 1;
	}

	iterations = atoi(argv[1]);
	host = strdup(argv[2]);
	port = atoi(argv[3]);
	rval = 0;
	
	/**Testing direct connection to master*/

	printf("Connecting to MySQL server through %s:%d.\n",host,port);
	begin = clock();

	for(i = 0;i<iterations;i++)
		{
			if((server = mysql_init(NULL)) == NULL){
				return 1;
			}
		    if(mysql_real_connect(server,host,"maxuser","maxpwd",NULL,port,NULL,0) == NULL){
				fprintf(stderr, "Failed to connect to database: Error: %s\n",
						mysql_error(server));
				rval = 1;
				break;
			}
			mysql_close(server);			
		}

	end = clock();
	baseline = (double)(end - begin)/CLOCKS_PER_SEC;

	free(host);
	host = strdup(argv[4]);
	port = atoi(argv[5]);

	/**Testing connection to master through MaxScale*/

	printf("Connecting to MySQL server through %s:%d.\n",host,port);
	begin = clock();

	for(i = 0;i<iterations;i++)
		{
		    if((server = mysql_init(NULL)) == NULL){
				return 1;
			}
		    if(mysql_real_connect(server,host,"maxuser","maxpwd",NULL,port,NULL,0) == NULL){
				rval = 1;
				fprintf(stderr, "Failed to connect to database: Error: %s\n",
						mysql_error(server));
				break;
			}
			mysql_close(server);			
		}

	end = clock();
	test = (double)(end - begin)/CLOCKS_PER_SEC;

	printf("CPU time used in seconds:\nDirect connection: %f\nThrough MaxScale: %f\n",baseline,test);
	
	free(host);
	return rval;
}
