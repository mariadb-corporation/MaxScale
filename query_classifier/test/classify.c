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
    "--no-defaults",
    "--datadir=.",
    "--language=.",
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
	int rd = 0,buffsz = getpagesize(),strsz = 0;
	char buffer[buffsz], *strbuff = (char*)calloc(buffsz,sizeof(char));

	mysql_library_init(num_elements, server_options, server_groups);

	while((rd = fread(buffer,sizeof(char),buffsz,stdin))){
		
		/**Fill the read buffer*/
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
		strsz += rd;

		char *tok,*nlptr;

		/**Remove newlines*/
		while((nlptr = strpbrk(strbuff,"\n")) != NULL && (nlptr - strbuff) < strsz){
			memmove(nlptr,nlptr+1,strsz - (nlptr + 1 - strbuff));
			strsz -= 1;
		}


		/**Parse read buffer for full queries*/

		while(strpbrk(strbuff,";") != NULL){
			tok = strpbrk(strbuff,";");
			int qlen = tok - strbuff;
			GWBUF* buff = gwbuf_alloc(qlen+5);
			*((unsigned char*)(buff->start)) = qlen;
			*((unsigned char*)(buff->start + 1)) = (qlen >> 8);
			*((unsigned char*)(buff->start + 2)) = (qlen >> 16);
			*((unsigned char*)(buff->start + 3)) = 0x00;
			*((unsigned char*)(buff->start + 4)) = 0x03;
			memcpy(buff->start+5, strbuff, qlen);
			memmove(strbuff,tok + 1, strsz - qlen);
			strsz -= qlen;
			memset(strbuff + strsz,0,buffsz - strsz);
			skygw_query_type_t type = query_classifier_get_type(buff);
			

			
			if(type == QUERY_TYPE_UNKNOWN){
				printf("QUERY_TYPE_UNKNOWN ");
			}
			if(type & QUERY_TYPE_LOCAL_READ){
				printf("QUERY_TYPE_LOCAL_READ ");
			}
			if(type & QUERY_TYPE_READ){
				printf("QUERY_TYPE_READ ");
			}
			if(type & QUERY_TYPE_WRITE){
				printf("QUERY_TYPE_WRITE ");
			}
			if(type & QUERY_TYPE_MASTER_READ){
				printf("QUERY_TYPE_MASTER_READ ");
			}
			if(type & QUERY_TYPE_SESSION_WRITE){
				printf("QUERY_TYPE_SESSION_WRITE ");
			}
			if(type & QUERY_TYPE_USERVAR_READ){
				printf("QUERY_TYPE_USERVAR_READ ");
			}
			if(type & QUERY_TYPE_SYSVAR_READ){
				printf("QUERY_TYPE_SYSVAR_READ ");
			}
			if(type & QUERY_TYPE_GSYSVAR_READ){
				printf("QUERY_TYPE_GSYSVAR_READ ");
			}
			if(type & QUERY_TYPE_GSYSVAR_WRITE){
				printf("QUERY_TYPE_GSYSVAR_WRITE ");
			}
			if(type & QUERY_TYPE_BEGIN_TRX){
				printf("QUERY_TYPE_BEGIN_TRX ");
			}
			if(type & QUERY_TYPE_ENABLE_AUTOCOMMIT){
				printf("QUERY_TYPE_ENABLE_AUTOCOMMIT ");
			}
			if(type & QUERY_TYPE_DISABLE_AUTOCOMMIT){
				printf("QUERY_TYPE_DISABLE_AUTOCOMMIT ");
			}
			if(type & QUERY_TYPE_ROLLBACK){
				printf("QUERY_TYPE_ROLLBACK ");
			}
			if(type & QUERY_TYPE_COMMIT){
				printf("QUERY_TYPE_COMMIT ");
			}
			if(type & QUERY_TYPE_PREPARE_NAMED_STMT){
				printf("QUERY_TYPE_PREPARE_NAMED_STMT ");
			}
			if(type & QUERY_TYPE_PREPARE_STMT){
				printf("QUERY_TYPE_PREPARE_STMT ");
			}
			if(type & QUERY_TYPE_EXEC_STMT){
				printf("QUERY_TYPE_EXEC_STMT ");
			}
			if(type & QUERY_TYPE_CREATE_TMP_TABLE){
				printf("QUERY_TYPE_CREATE_TMP_TABLE ");
			}
			if(type & QUERY_TYPE_READ_TMP_TABLE){
				printf("QUERY_TYPE_READ_TMP_TABLE ");
			}

			printf("\n");
			gwbuf_free(buff);			
		}

	}
	free(strbuff);
	return 0;
}
