#include <my_config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <query_classifier.h>
#include <buffer.h>
#include <mysql.h>

static char* server_options[] =
{
    "MariaDB Corporation MaxScale",
    "--datadir=./",
    "--language=./",
    "--skip-innodb",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] =
{
    "embedded",
    "server",
    "server",
    NULL
};

int main(int argc, char** argv)
{
    unsigned int psize;
    GWBUF* qbuff;
    char *tok;
    char readbuff[4092];
    FILE* infile;
    FILE* outfile;

    if (argc != 3)
    {
        printf("Usage: canonizer <input file> <output file>\n");
        return 1;
    }

    if (!utils_init())
    {
        printf("Utils library init failed.\n");
        return 1;
    }

    if (mysql_library_init(num_elements, server_options, server_groups))
    {
        printf("Embedded server init failed.\n");
        return 1;
    }

    infile = fopen(argv[1],"rb");
    outfile = fopen(argv[2],"wb");

    if (infile == NULL || outfile == NULL)
    {
        printf("Opening files failed.\n");
        return 1;
    }

    while (!feof(infile) && fgets(readbuff, 4092, infile))
    {
        char* nl = strchr(readbuff, '\n');
        if (nl)
        {
            *nl = '\0';
        }
        if (strlen(readbuff) > 0)
        {
            psize = strlen(readbuff);
            qbuff = gwbuf_alloc(psize + 7);
            *(qbuff->sbuf->data + 0) = (unsigned char) psize;
            *(qbuff->sbuf->data + 1) = (unsigned char) (psize >> 8);
            *(qbuff->sbuf->data + 2) = (unsigned char) (psize >> 16);
            *(qbuff->sbuf->data + 4) = 0x03;
            memcpy(qbuff->start + 5, readbuff, psize + 1);
            tok = qc_get_canonical(qbuff);
            fprintf(outfile, "%s\n", tok);
            free(tok);
            gwbuf_free(qbuff);
        }
    }
    fclose(infile);
    fclose(outfile);
    mysql_library_end();

    return 0;
}
