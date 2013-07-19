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
	char *input_data_padded = NULL;
	uint8_t output[1 + 128] = "";
	char hex_output[1 + 128 * 2] ="";
	int input_len = 0;

	char enc_key[1 + AES_BLOCK_SIZE * 2]="12345678901234567890123456789012";
	char iv[1 + AES_BLOCK_SIZE]="GW_SCALE_M_M_V__";

	AES_KEY ctx;
	AES_KEY dectx;

	if (argv[1]) {
		input_data = argv[1];
	} else {
		input_data = "|_AES256_input_text_|";
  	} 

	input_len = strlen(input_data) - 1 ;

	/* Please note AES_BLOCK_SIZE is 16 bytes */

	if (input_len >= 0)
		input_len = (( input_len / AES_BLOCK_SIZE ) + 1) * AES_BLOCK_SIZE;
	else
		input_len = AES_BLOCK_SIZE;

	if (input_len > strlen(input_data)) {
		input_data_padded = calloc(1, input_len);
		strcpy(input_data_padded, input_data);
	} else {
		input_data_padded = input_data;
	}

	fprintf(stderr,"OPENSSL: Input text [%s] is %i -> 16 bytes rounded is %i\n", input_data_padded, strlen(input_data_padded), input_len);

	/* Setting AES 256 ecryption */
	AES_set_encrypt_key(enc_key, (AES_BLOCK_SIZE * 2) * 8, &ctx);

	/* Let's encrypt the input text */
	AES_cbc_encrypt(input_data_padded, output, input_len, &ctx, iv, AES_ENCRYPT);

	/* Convert binary data to HEX: output size is twice the inoput */
	gw_bin2hex(hex_output, output, input_len);
    
	printf("\nEncrypted HEX is [%s]: keep it!\n", hex_output);

	exit(0);
}
