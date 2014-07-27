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
 * @mainpage Filter Harness
 *
 * A test harness that feeds a list of queries to a chain of filters and sends responses from a dummy backend.
 * The queries and replies to them can be logged into file and/or printed to the standard output and the contents of the
 * queries can either be read from a file or manually entered.
 *
 * The test harness supports reading pre-configured setups from standard INI files and uses the same syntax that MaxScale
 * configuration files use:
 *
 * @code
 * [TESTFILTER]
 * type=filter
 * module=testfiler
 * parameter="this is an option"
 * @endcode
 * Options for the configuration file 'harness.cnf'':
 *@code
 *	threads		Number of threads to use when routing buffers
 *	sessions	Number of sessions
 *@endcode
 * Options for the command line:
 *@code
 *	-h	Display this information
 *	-c	Path to the MaxScale configuration file to parse for filters
 *	-i	Name of the input file for buffers
 *	-o	Name of the output file for results
 *	-q	Suppress printing to stdout
 *	-t	Number of threads
 *	-s	Number of sessions
 *	-d	Routing delay
 *@endcode
 *
 *For a list of interactive mode commads, enter @c help as a command.
 *@n
 * @verbatim
  Revision History
 
  Date		Who			Description
  01/07/14	Markus Makela		Initial implementation
  27/07/14	Markus Makela		Added the clientReply interface
 
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

/**
 * Frees all the query buffers.
 */
void free_buffers();

/**
 * Frees all the loaded filters.
 */
void free_filters();

/**
 * Converts the passed string into an operation.
 *
 * @param tk The string to parse
 * @return The operation to perform or UNDEFINED, if parsing failed
 */
operation_t user_input(char* tk);

/**
 *Prints a list of available commands.
 */
void print_help();

/**
 * Prints the current status of loaded filters and queries, number of threads
 * and sessions and possible output files.
 */
void print_status();

/**
 *Opens a file for reading and/or writing with adequate permissions.
 *
 * @param str Path to file
 * @param write Non-zero for write permissions, zero for read only.
 * @return The assigned file descriptor or -1 in case an error occurred
 */
int open_file(char* str, unsigned int write);

/**
 * Reads filter parameters from the command line as name-value pairs.
 *
 *@param paramc The number of parameters read is assigned to this variable
 *@return The newly allocated list of parameters with the last one being NULL
 */
FILTER_PARAMETER** read_params(int* paramc);

/**
 * Dummy endpoint for the queries of the filter chain.
 *
 * Prints and logs the contents of the GWBUF after it has passed through all the filters.
 * The packet is handled as a COM_QUERY packet and the packet header is not printed.
 */
int routeQuery(void* instance, void* session, GWBUF* queue);

/**
 * Dummy endpoint for the replies of the filter chain.
 *
 * Prints and logs the contents of the GWBUF after it has passed through all the filters.
 * The packet is handled as a OK packet with no message and the packet header is not printed.
 */
int clientReply(void* ins, void* session, GWBUF* queue);

/**
 *Manual input of query thourgh the command line.
 */
void manual_query();

/**
 *Loads the contents of the current input file
 */
void load_query();

/**
 * Handler for the INI file parser that builds a linked list
 * of all the sections and their name-value pairs.
 * @param user Current configuration.
 * @param section Name of the section.
 * @param name Name of the item.
 * @param value Value of the item.
 * @return Non-zero on success, zero in case parsing is finished.
 * @see load_config()
 */
static int handler(void* user, const char* section, const char* name,const char* value);

/**
 * Removes all non-filter modules from the configuration
 *
 * @param conf A pointer to a configuration struct
 * @return The stripped version of the configuration
 * @see load_config()
 */
CONFIG* process_config(CONFIG* conf);

/**
 * Reads a MaxScale configuration (or any INI file using MaxScale notation) file and loads only the filter modules in it.
 * 
 * @param fname Configuration file name
 * @return Non-zero on success, zero in case an error occurred.
 */
int load_config(char* fname);

/**
 * Loads a new instance of a filter and starts a new session.
 * This function assumes that the filter module is already loaded.
 * Passing NULL as the CONFIG parameter causes the parameters to be
 * read from the command line one at a time.
 *
 * @param fc The FILTERCHAIN where the new instance and session are created
 * @param cnf A configuration read from a file 
 * @return 1 on success, 0 in case an error occurred
 * @see load_filter_module()
 */
int load_filter(FILTERCHAIN* fc, CONFIG* cnf);

/**
 * Loads the filter module and link it to the filter chain
 *
 * The downstream is set to point to the current head of the filter chain
 *
 * @param str Name of the filter module
 * @return Pointer to the newly initialized FILTER_CHAIN element or NULL in case module loading failed
 * @see load_filter()
 */
FILTERCHAIN* load_filter_module(char* str);

/**
 * Initializes the indexes used while routing buffers and prints the progress
 * of the routing process.
 */
void route_buffers();

/**
 * Worker function for threads.
 * Routes a query buffer if there are unrouted buffers left.
 *
 * @param thr_num ID number of the thread
 */
void work_buffer(void* thr_num);

/**
 * Generates a fake packet used to emulate a response from the backend.
 *
 * Current implementation only works with PACKET_OK and the packet has no message.
 * The caller is responsible for freeing the allocated memory by calling gwbuf_free().
 * @param pkt The packet type
 * @return The newly generated packet or NULL if an error occurred
 */
GWBUF* gen_packet(PACKET pkt);

/**
 * Process the command line parameters and the harness configuration file.
 *
 * Reads the contents of the 'harness.cnf' file and command line parameters
 * and parses them. Options are interpreted accoding to the following table.
 * If no command line arguments are given, interactive mode is used.
 *
 * By default if no input file is given or no configuration file or specific
 * filters are given, but other options are, the program exits with 0.
 *
 *
 * @param argc Number of arguments
 * @param argv List of argument strings
 * @return 1 if successful, 0 if no input file, configuration file or specific
 * filters are given, but other options are, or if an error occurs.
 */
int process_opts(int argc, char** argv);

#endif
