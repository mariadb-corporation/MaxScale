
#define PCRE2_CODE_UNIT_WIDTH 8
#define BUFFERSIZE 100
#include <stdio.h>
#include <string.h>
#include <pcre2.h>


int main(int argc, char **argv)
{
pcre2_code *re;
PCRE2_SPTR pattern;     
PCRE2_SPTR subject;     

int errornumber;
int i;
int rc;


PCRE2_SIZE erroroffset;
PCRE2_SIZE *ovector;

size_t subject_length;
pcre2_match_data *match_data;

 char buffer[BUFFERSIZE];


/* Read Query String from standard input here*/

printf("Enter a Query to be substituted: \n");
    while(fgets(buffer, BUFFERSIZE , stdin) != NULL)
    {
        printf("%s\n", buffer);
        break;
    }

pattern = (PCRE2_SPTR)"[0-9]";   // Replace this with regex for string concatenation
subject = (PCRE2_SPTR)buffer;
subject_length = strlen((char *)subject);


/* Compile regex and perform matching here  by wrapping match within CONCAT() and replace + with , */


re = pcre2_compile(
  pattern,               
  PCRE2_ZERO_TERMINATED,
  0,                     
  &errornumber,          
  &erroroffset,         
  NULL);                 


if (re == NULL)
  {
  PCRE2_UCHAR buffer[256];
  pcre2_get_error_message(errornumber, buffer, sizeof(buffer));
  printf("PCRE2 compilation failed at offset %d: %s\n", (int)erroroffset,
    buffer);
  return 1;
  }



match_data = pcre2_match_data_create_from_pattern(re, NULL);

rc = pcre2_match(
  re,                   
  subject,              
  subject_length,      
  0,                    
  0,                   
  match_data,          
  NULL);  


if (rc < 0)
  {
  switch(rc)
    {
    case PCRE2_ERROR_NOMATCH: printf("No match\n"); break;
    default: printf("Matching error %d\n", rc); break;
    }
  pcre2_match_data_free(match_data);   
  pcre2_code_free(re);                
  return 1;
  }


ovector = pcre2_get_ovector_pointer(match_data);
printf("\nMatch succeeded at offset %d\n", (int)ovector[0]);


/* Do Substitution for matches found here. by wrapping match within CONCAT() and replace + with ,.  For now only one match is handled*/


/* Use Connector C here to forward query that is rewritten to MariaDB/MySQL server */



printf("\n");
pcre2_match_data_free(match_data);
pcre2_code_free(re);
return 0;
}