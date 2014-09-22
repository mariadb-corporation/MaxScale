#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <query_classifier.h>
#include <buffer.h>
#include <mysql.h>
#include <unistd.h>

static char* server_options[] = {
	"SkySQL Gateway",
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

int main(int argc, char** argv)
{
	GWBUF* gwbuff;
	int rd = 0,buffsz = getpagesize(),strsz = buffsz;
	char buffer[buffsz], *strbuff = (char*)calloc(buffsz,sizeof(char));
	
	while((rd = fread(buffer,sizeof(char),buffsize,stdin))){
		
		if(strsz + rd >= buffsz){
			char* tmp = (char*)calloc((buffsz*2),sizeof(char));
			
			if(!tmp){
				fprintf(stderr,"Error: Cannot allocate enough memory.");
				return 1;
			}
			memcpy(tmp,strbuff,buffsz);
			free(strbuff);
			strbuff = tmp;
			buffsz *= 2;
		}
		
		memcpy(strbuff+strsz,buffer,rd);
		querysz += rd;
	}

	if(querysz > 0){
		printf("%s",strbuff);
		free(strbuff);
	}

	return 0;
}
