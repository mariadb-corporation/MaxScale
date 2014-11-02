#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <query_classifier.h>
#include <buffer.h>
#include <mysql.h>

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

/** 
 * @return 0 if succeed, 1 if failed
 */
int main(int argc, char** argv)
{
	int fdin;
	int fdout;
	int fsz;
	int i;
	int bsz = 4;
	int z=0;
	unsigned int psize;
	GWBUF** qbuff;
	char *qin;
	char *buffer;
	char *tok;
	bool failed;
	
	if (argc != 3)
	{
		printf("Usage: canonizer <input file> <output file>\n");
		return 1;
	}	
	failed = mysql_library_init(num_elements, server_options, server_groups);
	
	if (failed)
	{
		printf("Embedded server init failed.\n");
		return 1;
	}
	fdin = open(argv[1],O_RDONLY);
	fsz = lseek(fdin,0,SEEK_END);
	lseek(fdin,0,SEEK_SET);
	
	if (!(buffer = (char *)calloc(1, fsz+1))) 
	{
		mysql_library_end();
		printf("Error: Failed to allocate memory.");
		return 1;
	}
	read(fdin,buffer,fsz);
	
	qbuff = (GWBUF **)calloc(bsz, sizeof(GWBUF*));
	tok = strtok(buffer,"\n");
	i = 0;

	while(tok)
	{

		if (i>=bsz)
		{
			GWBUF** tmp = (GWBUF **)calloc(bsz*2, sizeof(GWBUF*));
			
			if (!tmp)
			{
				printf("Error: Failed to allocate memory.");
				return 1;	
			}
			
			for (z=0; z<bsz; z++)
			{
				tmp[z] = qbuff[z];
			}
			free(qbuff);
			qbuff = tmp;
			bsz *= 2;
		}
		
		if (strlen(tok) > 0)
		{
			qin = strdup(tok);
			psize = strlen(qin);
			qbuff[i] = gwbuf_alloc(psize + 6);
			*(qbuff[i]->sbuf->data + 0) = (unsigned char)psize;
			*(qbuff[i]->sbuf->data + 1) = (unsigned char)(psize>>8);
			*(qbuff[i]->sbuf->data + 2) = (unsigned char)(psize>>16);
			*(qbuff[i]->sbuf->data + 4) = 0x03;
			memcpy(qbuff[i]->sbuf->data + 5,qin,psize);
			*(qbuff[i]->sbuf->data + 5 + psize) = 0x00;
			tok = strtok(NULL,"\n\0");
			free(qin);
			i++;
		}
	}
	fdout = open(argv[2],O_TRUNC|O_CREAT|O_WRONLY,S_IRWXU|S_IXGRP|S_IXOTH);

	for (i = 0;i<bsz;i++)
	{
		if (qbuff[i])
		{
			parse_query(qbuff[i]);
			tok = skygw_get_canonical(qbuff[i]);
			write(fdout,tok,strlen(tok));
			write(fdout,"\n",1);
			gwbuf_free(qbuff[i]);
		}
	}
	free(qbuff);
	free(buffer);
	close(fdin);
	close(fdout);
	mysql_library_end();
	
	return 0;
}
