#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <openssl/aes.h>

int main(int argc, char *argv[])
{   
	char *input_data = NULL;
	unsigned char output[1 + 128]="";
	char original_data[1 + 128]="";
	char hex_output[1 + 128]="";
	uint8_t encrypted_data[1 + 128] ="";
	int input_len = 0;
	char stored_passwd[1 + 128]="5B2A43A3F04233652E44D34D123837C3F0659AEE03254AFFD7140CED5AAE231B";
	char dec_key[1 + AES_BLOCK_SIZE * 2]="12345678901234567890123456789012";
	char ivdec[1 + AES_BLOCK_SIZE]="GW_SCALE_M_M_V__";
	AES_KEY dectx;

	if (argv[1]) {
		input_data = argv[1];
	} else {
		input_data = stored_passwd;
	}
    
	fprintf(stderr,"OPENSSL: Input HEX to decode is [%s], %i bytes\n", input_data, strlen(input_data));

	AES_set_decrypt_key(dec_key, (AES_BLOCK_SIZE * 2) * 8, &dectx);

	gw_hex2bin(encrypted_data, input_data, strlen(input_data));
	input_len = strlen(stored_passwd) / 2;

	fprintf(stderr, "ENCRYPTED data from HEX is %i bytes long\n", input_len);

	AES_cbc_encrypt(encrypted_data, original_data, input_len, &dectx, ivdec, AES_DECRYPT);

	printf("\nCLEAR data is: [%s], %i bytes\n", original_data, strlen(original_data));


    exit(0);
}
