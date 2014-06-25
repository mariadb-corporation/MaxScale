/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright SkySQL Ab 2013
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <filter.h>
#include <buffer.h>
#include <skygw_utils.h>
/**
 * @file harness.h Test harness for independent testing of filters
 *
 * A test harness that feeds a GWBUF to a chain of filters and prints the results
 * either into a file or to the standard output. 
 *
 * The contents of the GWBUF are either manually set through the standard input
 * or read from a file. The filter chain can be modified and options for the
 * filters are read either from a configuration file or
 * interactively from the command line.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 25/06/14	Markus Makela		Initial implementation
 *
 * @endverbatim
 */

/**
 *A list of available actions.
 */

struct FILTERCHAIN_T
{
  FILTER* filter;
  struct FILTERCHAIN_T* next;

};

struct GWBUFCHAIN_T
{
  GWBUF* buffer;
  struct GWBUFCHAIN_T* next;

};
typedef struct FILTERCHAIN_T FILTERCHAIN;
typedef struct GWBUFCHAIN_T GWBUFCHAIN;

typedef struct
{
  int use_stdout;
  char* infile;
  char* outfile;
  FILTERCHAIN* head;
  GWBUFCHAIN* gwbuffer;

}HARNESS_INSTANCE;

static HARNESS_INSTANCE instance;

typedef enum 
  {
    RUN,    
    LOAD_FILTER,
    LOAD_CONFIG,
    SET_INFILE,
    SET_OUTFILE,
    CLEAR,
    HELP,
    QUIT,
    UNDEFINED
  } operation_t;

void clear();
operation_t user_input(char*);
void print_help();


int main(int argc, char** argv){
  int running = 1;
  char buffer[256];
  char* tk;
  char* tmp;
  instance.infile = malloc(256*sizeof(char));
  instance.outfile = malloc(256*sizeof(char));
  instance.head = malloc(sizeof(FILTERCHAIN));
  instance.head->next = NULL;
  instance.gwbuffer = malloc(sizeof(GWBUFCHAIN));
  instance.gwbuffer->next = NULL;
  instance.use_stdout = 1;
  if((tmp  = calloc(256, sizeof(char))) == NULL){
    printf("Error: Out of memory\n");
    return 1;
  }
  if(instance.infile == NULL || instance.outfile == NULL ||
 instance.head == NULL || instance.gwbuffer == NULL){
    printf("Error: Out of memory\n");
    return 1;
  } 
  while(running){
    printf("\nHarness> ");
    fgets(buffer,256,stdin);
    tk = strtok(buffer," ");
    switch(user_input(tk))
      {
      case RUN:

	break;

      case LOAD_FILTER:

	break;

      case LOAD_CONFIG:

	break;

      case SET_INFILE:

	tk = strtok(NULL," ");
	

	if(tmp == NULL){
	  printf("Error: Out of memory\n");
	  break;
	}

	strcat(tmp,tk);
	free(instance.infile);
	if(strlen(tmp) > 0){
	 instance.infile = strdup(tmp);
	}

	break;

      case SET_OUTFILE:

	tk = strtok(NULL," ");
	char* tmp = calloc(256, sizeof(char));

	if(tmp == NULL){
	  printf("Error: Out of memory\n");
	  break;
	}

	strcat(tmp,tk);
	free(instance.outfile);
	if(strlen(tmp) > 0){
	  instance.outfile = strdup(tmp);
	  instance.use_stdout = 0;
	}else{
	  instance.use_stdout = 1;
	}
	free(tmp);
	break;

      case CLEAR:
	
	clear();
	break;

      case HELP:

	print_help();	
	break;

      case QUIT:

	clear();
	running = 0;
	break;

      default:
	
	break;
	
      }  
    free(tmp);
  }
  return 0;
}
void clear()
{
	while(instance.head){
	  FILTERCHAIN* tmph = instance.head;
	  instance.head = instance.head->next;
	  free(tmph->filter);
	  free(tmph);
	}
	while(instance.gwbuffer){
	  GWBUFCHAIN* tmpb = instance.gwbuffer;
	  instance.gwbuffer = instance.gwbuffer->next;
	  free(tmpb->buffer);
	  free(tmpb);
	}
}
operation_t user_input(char* tk)
{

  if(strcmp(tk,"run")==0){
    return RUN;

  }else if(strcmp(tk,"add")==0){
    return LOAD_FILTER;

  }else if(strcmp(tk,"clear")==0){
    return CLEAR;

  }else if(strcmp(tk,"in")==0){
    return SET_INFILE;

  }else if(strcmp(tk,"out")==0){
    return SET_OUTFILE;

  }else if(strcmp(tk,"exit")==0 || strcmp(tk,"quit")==0 || strcmp(tk,"q")==0){
    return QUIT;

  }else if(strcmp(tk,"help")==0){
    return HELP;
  }
  return UNDEFINED;
}

/**
 *Prints a short description and a list of available commands.
 */
void print_help()
{

  printf("\nFilter Test Harness\n"
	 "List of commands:\n %8s%8s %8s%8s %8s%8s %8s%8s %8s%8s %8s%8s %8s%8s"
	 ,"help","Prints this help message.\n"
	 ,"run","Feeds the contents of the buffer to the filter chain.\n"
	 ,"add <filter name>","Loads a filter and appeds it to the end of the chain.\n"
	 ,"clear","Clears the filter chain.\n"
	 ,"in <file name>","Source file for the SQL statements.\n"
	 ,"out <file name>","Destination file for the SQL statements. Defaults to stdout if no parameters were passed.\n"
	 ,"exit","Quits the program\n"
	 );

}







