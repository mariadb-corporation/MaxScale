#ifndef _FILTER_HARNESS_H
#define _FILTER_HARNESS_H
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

/**
 * @file harness.h Test harness for independent testing of filters
 *
 * A test harness that feeds a GWBUF to a chain of filters and prints the results
 * either into a file or to the standard output. 
 *
 * The contents of the GWBUF and the filter parameters are either manually set through
 * the command line or read from a file.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 01/07/14	Markus Makela		Initial implementation
 *
 * @endverbatim
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <filter.h>
#include <buffer.h>
#include <modules.h>
#include <modutil.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <atomic.h>
#include <ini.h>

/**
 * A single name-value pair and a link to the next item in the 
 * configuration.
 */
typedef struct CONFIG_ITEM_T
{
  char* name;
  char* value;
  struct CONFIG_ITEM_T* next;
}CONFIG_ITEM;

/**
 *A simplified version of a MaxScale configuration context used to load filters
 * and their options.
 */
typedef struct CONFIG_T
{
  char* section;
  CONFIG_ITEM* item;
  struct CONFIG_T* next;
  
}CONFIG;

/**
 *A structure that holds all the necessary information to emulate a working
 * filter environment.
 */
struct FILTERCHAIN_T
{
  FILTER* filter; /**An instance of a particular filter*/
  FILTER_OBJECT* instance; /**Dynamically loaded module*/
  SESSION** session; /**A list of sessions*/
  DOWNSTREAM** down; /** A list of next filters downstreams*/
  UPSTREAM** up; /** A list of next filters upstreams*/
  char* name; /**Module name*/
  struct FILTERCHAIN_T* next;
};

typedef struct FILTERCHAIN_T FILTERCHAIN;

/**
 * A container for all the filters, query buffers and user specified parameters
 */
typedef struct
{
  int running;
  int verbose; /**Whether to print to stdout*/
  int infile; /**A file where the queries are loaded from*/
  char* infile_name;
  int outfile; /**A file where the output of the filters is logged*/
  char* outfile_name;
  FILTERCHAIN* head; /**The head of the filter chain*/
  FILTERCHAIN* tail; /**The tail of the filter chain*/
  GWBUF** buffer; /**Buffers that are fed to the filter chain*/
  int buffer_count;
  int session_count;
  DOWNSTREAM dummyrouter; /**Dummy downstream router for data extraction*/
  UPSTREAM dummyclient; /**Dummy downstream router for data extraction*/
  CONFIG* conf; /**Configurations loaded from a file*/
  pthread_mutex_t work_mtx; /**Mutex for buffer routing*/
  int buff_ind; /**Index of first unrouted buffer*/
  int sess_ind;/**Index of first unused session*/
  int last_ind; /**Index of last used session*/
  pthread_t* thrpool;
  int thrcount; /**Number of active threads*/
  int rt_delay; /**Delay each thread waits after routing a query, in milliseconds*/
}HARNESS_INSTANCE;

static HARNESS_INSTANCE instance;

/**
 *A list of available actions.
 */

typedef enum 
  {
    UNDEFINED,
    RUNFILTERS,
    LOAD_FILTER,
    DELETE_FILTER,
    LOAD_CONFIG,
    SET_INFILE,
    SET_OUTFILE,
    THR_COUNT,
    SESS_COUNT,
    OK,
    QUIT
  } operation_t;

typedef enum
  {
    PACKET_OK,
    PACKET_ERROR,
    PACKET_RESULT_SET
  } packet_t;

typedef packet_t PACKET;

void free_buffers();
void free_filters();
operation_t user_input(char*);
void print_help();
void print_status();
int open_file(char* str, unsigned int write);
FILTER_PARAMETER** read_params(int*);
int routeQuery(void* instance, void* session, GWBUF* queue);
void manual_query();
void load_query();
static int handler(void* user, const char* section, const char* name,const char* value);
CONFIG* process_config(CONFIG*);
FILTERCHAIN* load_filter_module(char* str);
int load_filter(FILTERCHAIN*, CONFIG*);
int load_config(char* fname);
void route_buffers();
void work_buffer(void*);
GWBUF* gen_packet(PACKET);
int process_opts(int argc, char** argv);

#endif
