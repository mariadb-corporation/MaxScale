/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file fwfilter.c
 * Firewall Filter
 *
 * A filter that acts as a firewall, denying queries that do not meet a set of rules.
 *
 * Filter configuration parameters:
 *
 *		rules=<path to file>			Location of the rule file
 *
 * Rules are defined in a separate rule file that lists all the rules and the users to whom the rules are applied.
 * Rules follow a simple syntax that denies the queries that meet the requirements of the rules.
 * For example, to define a rule denying users from accessing the column 'salary' between
 * the times 15:00 and 17:00, the following rule is to be configured into the configuration file:
 *
 *		rule block_salary deny columns salary at_times 15:00:00-17:00:00
 *
 * The users are matched by username and network address. Wildcard values can be provided by using the '%' character.
 * For example, to apply this rule to users John, connecting from any address
 * that starts with the octets 198.168.%, and Jane, connecting from the address 192.168.0.1:
 *
 *		users John@192.168.% Jane@192.168.0.1 match any rules block_salary
 *
 *
 * The 'match' keyword controls the way rules are matched. If it is set to 'any' the first active rule that is triggered will cause the query to be denied.
 * If it is set to 'all' all the active rules need to match before the query is denied.
 *
 * Rule syntax
 *
 * rule NAME deny [wildcard | columns VALUE ... | regex REGEX | limit_queries COUNT TIMEPERIOD HOLDOFF | no_where_clause] [at_times VALUE...] [on_queries [select|update|insert|delete]]
 *
 * User syntax
 *
 * users NAME ... match [any|all] rules RULE ...
 *
 */
#include <my_config.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <filter.h>
#include <string.h>
#include <atomic.h>
#include <modutil.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <mysql_client_server_protocol.h>
#include <spinlock.h>
#include <session.h>
#include <plugin.h>
#include <skygw_types.h>
#include <skygw_debug.h>
#include <time.h>
#include <assert.h>
#include <regex.h>
MODULE_INFO 	info = {
    MODULE_API_FILTER,
	MODULE_ALPHA_RELEASE,
	FILTER_VERSION,
	"Firewall Filter"
};

static char *version_str = "V1.0.0";

/*
 * The filter entry points
 */
static	FILTER	*createInstance(char **options, FILTER_PARAMETER **);
static	void	*newSession(FILTER *instance, SESSION *session);
static	void 	closeSession(FILTER *instance, void *session);
static	void 	freeSession(FILTER *instance, void *session);
static	void	setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static	int	routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static	void	diagnostic(FILTER *instance, void *fsession, DCB *dcb);

static FILTER_OBJECT MyObject = {
    createInstance,
	newSession,
	closeSession,
	freeSession,
	setDownstream,
	NULL, 
	routeQuery,
	NULL,
	diagnostic,
};


/**
 * Rule types
 */
typedef enum {
    RT_UNDEFINED = 0x00,
    RT_COLUMN,
	RT_THROTTLE,
	RT_PERMISSION,
	RT_WILDCARD,
	RT_REGEX,
	RT_CLAUSE
}ruletype_t;

const char* rule_names[] = {
    "UNDEFINED",
	"COLUMN",
	"THROTTLE",
	"PERMISSION",
	"WILDCARD",
	"REGEX",
	"CLAUSE"
};


/**
 * Linked list of strings.
 */
typedef struct strlink_t{
    struct strlink_t *next;
    char* value;
}STRLINK;

typedef struct timerange_t{
    struct timerange_t* next;
    struct tm start;
    struct tm end;
}TIMERANGE;

typedef struct queryspeed_t{
    time_t first_query;
    time_t triggered;
    double period;
    double cooldown;	
    int count;
    int limit;
    long id;
    struct queryspeed_t* next;
}QUERYSPEED;


/**
 * A structure used to identify individual rules and to store their contents
 * 
 * Each type of rule has different requirements that are expressed as void pointers.
 * This allows to match an arbitrary set of rules against a user.
 */
typedef struct rule_t{
    void*		data;
    char*		name;
    ruletype_t	type;
    skygw_query_op_t on_queries;
    bool		allow;
    int times_matched;
    TIMERANGE* active;
}RULE;

/**
 * Linked list of pointers to a global pool of RULE structs
 */
typedef struct rulelist_t{
    RULE*				rule;
    struct rulelist_t*	next;
}RULELIST;

typedef struct user_t{
    char* name;
    SPINLOCK* lock;
    QUERYSPEED* qs_limit;
    RULELIST* rules_or;
    RULELIST* rules_and;
}USER;

/**
 * Linked list of IP adresses and subnet masks
 */
typedef struct iprange_t{
	struct iprange_t* next;
	uint32_t ip;
	uint32_t mask;
}IPRANGE;

/**
 * The Firewall filter instance.
 */
typedef struct {
	HASHTABLE* htable; 
	RULELIST* rules;
	STRLINK* userstrings;
	bool def_op;
	SPINLOCK* lock;
	long idgen; /**UID generator*/
} FW_INSTANCE;

/**
 * The session structure for Firewall filter.
 */
typedef struct {
	SESSION*	session;
	char* errmsg;
	DOWNSTREAM	down;
	UPSTREAM	up;
} FW_SESSION;

static int hashkeyfun(void* key);
static int hashcmpfun (void *, void *);

static int hashkeyfun(
					  void* key)
{
	if(key == NULL){
		return 0;
	}
	unsigned int hash = 0,c = 0;
	char* ptr = (char*)key;
	while((c = *ptr++)){
		hash = c + (hash << 6) + (hash << 16) - hash;
	}
	return (int)hash > 0 ? hash : -hash; 
}

static int hashcmpfun(
					  void* v1,
					  void* v2)
{
	char* i1 = (char*) v1;
	char* i2 = (char*) v2;

	return strcmp(i1,i2);
}

static void* hstrdup(void* fval)
{
	char* str = (char*)fval;
	return strdup(str);
}


static void* hstrfree(void* fval)
{
	free (fval);
	return NULL;
}


void* rlistdup(void* fval)
{
	
	RULELIST *rule = NULL,
		*ptr = (RULELIST*)fval;


	while(ptr){
		RULELIST* tmp = (RULELIST*)malloc(sizeof(RULELIST));
		tmp->next = rule;
		tmp->rule = ptr->rule;
		rule = tmp;
		ptr = ptr->next;
	}
	
	return (void*)rule;

}

static void* hrulefree(void* fval)
{
	USER* user = (USER*)fval;
	RULELIST *ptr = user->rules_or,*tmp;
	while(ptr){
		tmp = ptr;
		ptr = ptr->next;
		free(tmp);
	}
	ptr = user->rules_and;
	while(ptr){
		tmp = ptr;
		ptr = ptr->next;
		free(tmp);
	}
	free(user->name);
	free(user);
	return NULL;
}


/**
 * Strips the single or double quotes from a string.
 * This function modifies the passed string.
 * @param str String to parse
 * @return Pointer to the modified string
 */
char* strip_tags(char* str)
{

	assert(str != NULL);

	char *ptr = str,*re_start = NULL;
	bool found = false;
	while(*ptr != '\0'){

		if(*ptr == '"' ||
		   *ptr == '\''){
			if(found){
				*ptr = '\0';
				memmove(str,re_start,ptr - re_start);
				break;
			}else{
				*ptr = ' ';
				re_start = ptr + 1;
				found = true;
			}
		}
		ptr++;

	}

	return str;
}

/**
 * Parses a string that contains an IP address and converts the last octet to '%'.
 * This modifies the string passed as the parameter.
 * @param str String to parse
 * @return Pointer to modified string or NULL if an error occurred or the string can't be made any less specific
 */
char* next_ip_class(char* str)
{
	assert(str != NULL);

	/**The least specific form is reached*/
	if(*str == '%'){
		return NULL;
	}

	char* ptr = strchr(str,'\0');

	if(ptr == NULL){
		return NULL;
	}

	while(ptr > str){
		ptr--;
		if(*ptr == '.' && *(ptr+1) != '%'){
			break;
		}
	}
	
	if(ptr == str){
		*ptr++ = '%';
		*ptr = '\0';
		return str;
	}

	*++ptr = '%';
	*++ptr = '\0';


	return str;
}
/**
 * Parses the strign for the types of queries this rule should be applied to.
 * @param str String to parse
 * @param rule Poiter to a rule
 * @return True if the string was parses successfully, false if an error occurred
 */
bool parse_querytypes(char* str,RULE* rule)
{
	char buffer[512];
	char *ptr,*dest;
	bool done = false;
	rule->on_queries = 0;
	ptr = str;
	dest = buffer;

	while(ptr - buffer < 512)
    {
        if(*ptr == '|' || *ptr == ' ' ||  (done = *ptr == '\0')){
            *dest = '\0';
            if(strcmp(buffer,"select") == 0){
                rule->on_queries |= QUERY_OP_SELECT;
            }else if(strcmp(buffer,"insert") == 0){
                rule->on_queries |= QUERY_OP_INSERT;
            }else if(strcmp(buffer,"update") == 0){
                rule->on_queries |= QUERY_OP_UPDATE;
            }else if(strcmp(buffer,"delete") == 0){
                rule->on_queries |= QUERY_OP_DELETE;
            }

            if(done){
                return true;
            }

            dest = buffer;
            ptr++;
        }else{
            *dest++ = *ptr++;
        }
    }
	return false;	
}

/**
 * Checks whether a null-terminated string contains two ISO-8601 compliant times separated
 * by a single dash.
 * @param str String to check
 * @return True if the string is valid
 */
bool check_time(char* str)
{
	assert(str != NULL);

	char* ptr = str;
	int colons = 0,numbers = 0,dashes = 0;
    while(*ptr){
		if(isdigit(*ptr)){numbers++;}
		else if(*ptr == ':'){colons++;}
		else if(*ptr == '-'){dashes++;}
		ptr++;
	}
	return numbers == 12 && colons == 4 && dashes == 1;
}


#ifdef SS_DEBUG
#define CHK_TIMES(t)(ss_dassert(t->tm_sec > -1 && t->tm_sec < 62        \
                                && t->tm_min > -1 && t->tm_min < 60     \
                                && t->tm_hour > -1 && t->tm_hour < 24))
#else
#define CHK_TIMES(t)
#endif

#define IS_RVRS_TIME(tr) (mktime(&tr->end) < mktime(&tr->start))
/**
 * Parses a null-terminated string into two tm_t structs that mark a timerange
 * @param str String to parse
 * @param instance FW_FILTER instance
 * @return If successful returns a pointer to the new TIMERANGE instance. If errors occurred or
 * the timerange was invalid, a NULL pointer is returned.
 */
TIMERANGE* parse_time(char* str, FW_INSTANCE* instance)
{

	TIMERANGE* tr = NULL;
	int intbuffer[3];
	int* idest = intbuffer;
	char strbuffer[3];
	char *ptr,*sdest;
	struct tm* tmptr;

	assert(str != NULL && instance != NULL);
	
	tr = (TIMERANGE*)calloc(1,sizeof(TIMERANGE));

	if(tr == NULL){
		skygw_log_write(LOGFILE_ERROR, "fwfilter: malloc returned NULL.");		
		return NULL;
	}

	memset(&tr->start,0,sizeof(struct tm));
	memset(&tr->end,0,sizeof(struct tm));
	ptr = str;
	sdest = strbuffer;
	tmptr = &tr->start;

	while(ptr - str < 19){
		if(isdigit(*ptr)){
			*sdest = *ptr;
		}else if(*ptr == ':' ||*ptr == '-' || *ptr == '\0'){
			*sdest = '\0';
			*idest++ = atoi(strbuffer);
			sdest = strbuffer;
			
			if(*ptr == '-' || *ptr == '\0'){
				
				tmptr->tm_hour = intbuffer[0];
				tmptr->tm_min = intbuffer[1];
				tmptr->tm_sec = intbuffer[2];

				CHK_TIMES(tmptr);

				if(*ptr == '\0'){
					return tr;
				}

				idest = intbuffer;
				tmptr = &tr->end;
			}
			ptr++;
			continue;
		}
		ptr++;
		sdest++;
	}

	
	return tr;
}


/**
 * Splits the reversed timerange into two.
 *@param tr A reversed timerange
 *@return If the timerange is reversed, returns a pointer to the new TIMERANGE otherwise returns a NULL pointer
 */
TIMERANGE* split_reverse_time(TIMERANGE* tr)
{
	TIMERANGE* tmp = NULL;
	
	if(IS_RVRS_TIME(tr)){
	 	tmp = (TIMERANGE*)calloc(1,sizeof(TIMERANGE)); 
	 	tmp->next = tr; 
		tmp->start.tm_hour = 0; 
	 	tmp->start.tm_min = 0; 
		tmp->start.tm_sec = 0; 
		tmp->end = tr->end; 
	 	tr->end.tm_hour = 23; 
	 	tr->end.tm_min = 59; 
	 	tr->end.tm_sec = 59; 
	} 

	return tmp;
}

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
* The module initialisation routine, called when the module
* is first loaded.
*/
void
ModuleInit()
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
FILTER_OBJECT *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Finds the rule with a name matching the passed string.
 *
 * @param tok Name to search for
 * @param instance A valid FW_FILTER instance
 * @return A pointer to the matching RULE if found, else returns NULL
 */
RULE* find_rule(char* tok, FW_INSTANCE* instance)
{
	RULELIST* rlist = instance->rules;

	while(rlist){
		if(strcmp(rlist->rule->name,tok) == 0){
			return rlist->rule;
		}
		rlist = rlist->next;
	}
	skygw_log_write(LOGFILE_ERROR, "fwfilter: Rule not found: %s",tok);	
	return NULL;
}

/**
 * Adds the given rule string to the list of strings to be parsed for users.
 * @param rule The rule string, assumed to be null-terminated
 * @param instance The FW_FILTER instance
 */
void add_users(char* rule, FW_INSTANCE* instance)
{
	assert(rule != NULL && instance != NULL);

	STRLINK* link = calloc(1,sizeof(STRLINK));
	link->next = instance->userstrings;
	link->value = strdup(rule);
	instance->userstrings = link;
}

/**
 * Parses the list of rule strings for users and links them against the listed rules.
 * Only adds those rules that are found. If the rule isn't found a message is written to the error log.
 * @param rule Rule string to parse
 * @param instance The FW_FILTER instance
 */
void link_rules(char* rule, FW_INSTANCE* instance)
{
	assert(rule != NULL && instance != NULL);
	
	/**Apply rules to users*/

	bool match_any;
	char *tok, *ruleptr, *userptr, *modeptr;
	RULELIST* rulelist = NULL;

	userptr = strstr(rule,"users ");
	modeptr = strstr(rule," match ");
	ruleptr = strstr(rule," rules ");

	if((userptr == NULL || ruleptr == NULL || modeptr == NULL)||
	   (userptr > modeptr || userptr > ruleptr || modeptr > ruleptr)) {
		skygw_log_write(LOGFILE_ERROR, "fwfilter: Rule syntax incorrect, right keywords not found in the correct order: %s",rule);	
		return;
	}
		
	*modeptr++ = '\0';
	*ruleptr++ = '\0';

	tok = strtok(modeptr," ");
	if(strcmp(tok,"match") == 0){
		tok = strtok(NULL," ");
		if(strcmp(tok,"any") == 0){
			match_any = true;
		}else if(strcmp(tok,"all") == 0){
			match_any = false;
		}else{
			skygw_log_write(LOGFILE_ERROR, "fwfilter: Rule syntax incorrect, 'match' was not followed by 'any' or 'all': %s",rule);
			return;
		}
	}
	
	tok = strtok(ruleptr," ");
	tok = strtok(NULL," ");
		
	while(tok)
    {
        RULE* rule_found = NULL;
				
        if((rule_found = find_rule(tok,instance)) != NULL)
        {
            RULELIST* tmp_rl = (RULELIST*)calloc(1,sizeof(RULELIST));
            tmp_rl->rule = rule_found;
            tmp_rl->next = rulelist;
            rulelist = tmp_rl;

        }
        tok = strtok(NULL," ");
    }

	/**
	 * Apply this list of rules to all the listed users
	 */	

	*(ruleptr) = '\0';
	userptr = strtok(rule," ");
	userptr = strtok(NULL," ");

	while(userptr)
    {
        USER* user;
        RULELIST *tl = NULL,*tail = NULL;

        if((user = (USER*)hashtable_fetch(instance->htable,userptr)) == NULL){

            /**New user*/
            user = (USER*)calloc(1,sizeof(USER));

            if(user == NULL){
                free(rulelist);
                return;
            }
				
            if((user->lock = (SPINLOCK*)malloc(sizeof(SPINLOCK))) == NULL){
                free(user);
                free(rulelist);
                return;
            }

            spinlock_init(user->lock);
        }

        user->name = (char*)strdup(userptr);
        user->qs_limit = NULL;
        tl = (RULELIST*)rlistdup(rulelist);
        tail = tl;
        while(tail && tail->next){
            tail = tail->next;
        }

			
        if(match_any){
            tail->next = user->rules_or;
            user->rules_or = tl;
        }else{
            tail->next = user->rules_and;
            user->rules_and = tl;
        }
		    
        hashtable_add(instance->htable,
                      (void *)userptr,
                      (void *)user);				
			
        userptr = strtok(NULL," ");
		
    }
	
}


/**
 * Parse the configuration value either as a new rule or a list of users.
 * @param rule The string to parse
 * @param instance The FW_FILTER instance
 */
void parse_rule(char* rule, FW_INSTANCE* instance)
{
	ss_dassert(rule != NULL && instance != NULL);

	char *rulecpy = strdup(rule);
	char *tok = strtok(rulecpy," ,");
	bool allow,deny,mode;
	RULE* ruledef = NULL;
	
	if(tok == NULL) goto retblock;

	if(strcmp("rule",tok) == 0){ /**Define a new rule*/

		tok = strtok(NULL," ,");
		
		if(tok == NULL) goto retblock;
		
		RULELIST* rlist = NULL;

		ruledef = (RULE*)calloc(1,sizeof(RULE));
                
                if(ruledef == NULL)
                {
                    skygw_log_write(LOGFILE_ERROR,"Error : Memory allocation failed.");
                    goto retblock;
                }
                
		rlist = (RULELIST*)calloc(1,sizeof(RULELIST));
                
                if(rlist == NULL)
                {
                    free(ruledef);
                    skygw_log_write(LOGFILE_ERROR,"Error : Memory allocation failed.");
                    goto retblock;
                }
		ruledef->name = strdup(tok);
		ruledef->type = RT_UNDEFINED;
		ruledef->on_queries = QUERY_OP_UNDEFINED;
		rlist->rule = ruledef;
		rlist->next = instance->rules;
		instance->rules = rlist;

	}else if(strcmp("users",tok) == 0){

		/**Apply rules to users*/
		add_users(rule, instance);
		goto retblock;
	}

	tok = strtok(NULL, " ,");


	if((allow = (strcmp(tok,"allow") == 0)) || 
	   (deny = (strcmp(tok,"deny") == 0))){

		mode = allow ? true:false;
		ruledef->allow = mode;
		ruledef->type = RT_PERMISSION;
		tok = strtok(NULL, " ,");
			

		while(tok){
			if(strcmp(tok,"wildcard") == 0)
            {
                ruledef->type = RT_WILDCARD;
            }
			else if(strcmp(tok,"columns") == 0)
            {
                STRLINK *tail = NULL,*current;
                ruledef->type = RT_COLUMN;
                tok = strtok(NULL, " ,");
                while(tok && strcmp(tok,"at_times") != 0){
                    current = malloc(sizeof(STRLINK));
                    current->value = strdup(tok);
                    current->next = tail;
                    tail = current;
                    tok = strtok(NULL, " ,");
                }
			
                ruledef->data = (void*)tail;
                continue;

            }
			else if(strcmp(tok,"at_times") == 0)
            {

                tok = strtok(NULL, " ,");
                TIMERANGE *tr = NULL;
                while(tok){
                    TIMERANGE *tmp = parse_time(tok,instance);
			
                    if(IS_RVRS_TIME(tmp)){
                        tmp = split_reverse_time(tmp);
                    }
                    tmp->next = tr;
                    tr = tmp;
                    tok = strtok(NULL, " ,");
                }
                ruledef->active = tr;
            }
			else if(strcmp(tok,"regex") == 0)
            {
                bool escaped = false;
                regex_t *re;
                char* start, *str;
                tok = strtok(NULL," ");
					
                while(*tok == '\'' || *tok == '"'){
                    tok++;
                }

                start = tok;
					
                while(isspace(*tok) || *tok == '\'' || *tok == '"'){
                    tok++;
                }
					
                while(true){

                    if((*tok == '\'' || *tok == '"') && !escaped){
                        break;
                    }
                    escaped = (*tok == '\\');
                    tok++;
                }

                str = calloc(((tok - start) + 1),sizeof(char));
                if(str == NULL)
                {
                    skygw_log_write_flush(LOGFILE_ERROR, "Fatal Error: malloc returned NULL.");
                    goto retblock;
                }
                re = (regex_t*)malloc(sizeof(regex_t));

                if(re == NULL){
                    skygw_log_write_flush(LOGFILE_ERROR, "Fatal Error: malloc returned NULL.");	
                    free(str);
                    goto retblock;
                }

                memcpy(str, start, (tok-start));

                if(regcomp(re, str,REG_NOSUB)){
                    skygw_log_write(LOGFILE_ERROR, "fwfilter: Invalid regular expression '%s'.", str);
                    free(re);
                }
                else
                {
                    ruledef->type = RT_REGEX;
                    ruledef->data = (void*) re;
                }
                free(str);

            }
			else if(strcmp(tok,"limit_queries") == 0)
            {
					
                QUERYSPEED* qs = (QUERYSPEED*)calloc(1,sizeof(QUERYSPEED));

                spinlock_acquire(instance->lock);
                qs->id = ++instance->idgen;
                spinlock_release(instance->lock);

                tok = strtok(NULL," ");
                qs->limit = atoi(tok);

                tok = strtok(NULL," ");
                qs->period = atof(tok);
                tok = strtok(NULL," ");
                qs->cooldown = atof(tok);
                ruledef->type = RT_THROTTLE;
                ruledef->data = (void*)qs;
            }
			else if(strcmp(tok,"no_where_clause") == 0)
            {
                ruledef->type = RT_CLAUSE;
                ruledef->data = (void*)mode;
            }
			else if(strcmp(tok,"on_operations") == 0)
            {
                tok = strtok(NULL," ");
                if(!parse_querytypes(tok,ruledef)){
                    skygw_log_write(LOGFILE_ERROR,
                                    "fwfilter: Invalid query type"
                                    "requirements on where/having clauses: %s."
                                    ,tok);
                }	
            }
			tok = strtok(NULL," ,");
		}

		goto retblock;
	}

    retblock:
	free(rulecpy);

}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 * 
 * @param options	The options for this filter
 *
 * @return The instance data for this new instance
 */
static	FILTER	*
createInstance(char **options, FILTER_PARAMETER **params)
{
	FW_INSTANCE	*my_instance;
  	int i;
	HASHTABLE* ht;
	STRLINK *ptr,*tmp;
	char *filename = NULL, *nl;
	char buffer[2048];
	FILE* file;
	
	if ((my_instance = calloc(1, sizeof(FW_INSTANCE))) == NULL ||
		(my_instance->lock = (SPINLOCK*)malloc(sizeof(SPINLOCK))) == NULL){
            skygw_log_write(LOGFILE_ERROR, "Memory allocation for firewall filter failed.");
		return NULL;
	}
	
	spinlock_init(my_instance->lock);

	if((ht = hashtable_alloc(7, hashkeyfun, hashcmpfun)) == NULL){
		skygw_log_write(LOGFILE_ERROR, "Unable to allocate hashtable.");
		free(my_instance);
		return NULL;
	}

	hashtable_memory_fns(ht,hstrdup,NULL,hstrfree,hrulefree);
	
	my_instance->htable = ht;
	my_instance->def_op = true;

	for(i = 0;params[i];i++){
		if(strcmp(params[i]->name, "rules") == 0){
                    
                        if(filename)
                            free(filename);
                    
			filename = strdup(params[i]->value);
		}
	}
        
        if(filename == NULL)
        {
            skygw_log_write(LOGFILE_ERROR, "Unable to find rule file for firewall filter.");
            free(my_instance);
            return NULL;
        }
        
	if((file = fopen(filename,"rb")) == NULL ){
            skygw_log_write(LOGFILE_ERROR, "Error while opening rule file for firewall filter.");
		free(my_instance);
		free(filename);
		return NULL;
	}

	free(filename);
	
	while(!feof(file))
    {

        if(fgets(buffer,2048,file) == NULL){
            if(ferror(file)){
                skygw_log_write(LOGFILE_ERROR, "Error while reading rule file for firewall filter.");
                fclose(file);
                free(my_instance);
                return NULL;
            }
				
            if(feof(file)){
                break;
            }
        }	
        
        if((nl = strchr(buffer,'\n')) != NULL && ((char*)nl - (char*)buffer) < 2048){
            *nl = '\0';
        }
        
        parse_rule(buffer,my_instance);
    }

	fclose(file);
	
	/**Apply the rules to users*/
	ptr = my_instance->userstrings;
	while(ptr){
		link_rules(ptr->value,my_instance);
		tmp = ptr;
		ptr = ptr->next;
		free(tmp->value);
		free(tmp);
	}

	return (FILTER *)my_instance;
}




/**
 * Associate a new session with this instance of the filter and opens
 * a connection to the server and prepares the exchange and the queue for use.
 *
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{
	FW_SESSION	*my_session;

	if ((my_session = calloc(1, sizeof(FW_SESSION))) == NULL){
		return NULL;
	}
	my_session->session = session;
	return my_session;
}



/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
}

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	if(my_session->errmsg){
		free(my_session->errmsg);
		
	}
	free(my_session);
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session 
 * @param downstream	The downstream filter or router.
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	my_session->down = *downstream;
}

/**
 * Generates a dummy error packet for the client with a custom message.
 * @param session The FW_SESSION object
 * @param msg Custom error message for the packet.
 * @return The dummy packet or NULL if an error occurred
 */
GWBUF* gen_dummy_error(FW_SESSION* session, char* msg)
{
	GWBUF* buf;
	char* errmsg; 
	DCB* dcb;
	MYSQL_session* mysql_session;
	unsigned int errlen;
        
        if(session == NULL || session->session == NULL ||
           session->session->data == NULL ||
           session->session->client == NULL)
        {
            skygw_log_write_flush(LOGFILE_ERROR, "Error : Firewall filter session missing data.");
            return NULL;
        }
	
        dcb = session->session->client;
        mysql_session = (MYSQL_session*)session->session->data;
	errlen = msg != NULL ? strlen(msg) : 0; 
	errmsg = (char*)malloc((512 + errlen)*sizeof(char));
	
	if(errmsg == NULL){
		skygw_log_write_flush(LOGFILE_ERROR, "Fatal Error: Memory allocation failed.");	
		return NULL;
	}


	if(mysql_session->db[0] == '\0')
    {
        sprintf(errmsg,
                "Access denied for user '%s'@'%s'",
                dcb->user,
                dcb->remote);	
    }else
    {
        sprintf(errmsg,
                "Access denied for user '%s'@'%s' to database '%s'",
                dcb->user,
                dcb->remote,
                mysql_session->db);	
    }

	if(msg != NULL){
		char* ptr = strchr(errmsg,'\0');
		sprintf(ptr,": %s",msg);	
		
	}
	
	buf = modutil_create_mysql_err_msg(1,0,1141,"HY000", (const char*)errmsg);
        free(errmsg);
        
	return buf;
}

/**
 * Checks if the timerange object is active.
 * @return Whether the timerange is active
 */ 
bool inside_timerange(TIMERANGE* comp)
{

	struct tm* tm_now;
	struct tm tm_before,tm_after;
	time_t before,after,now, time_now;
	double to_before,to_after;
	
	time(&time_now);
	tm_now = localtime(&time_now);
	memcpy(&tm_before,tm_now,sizeof(struct tm));
	memcpy(&tm_after,tm_now,sizeof(struct tm));

	
	tm_before.tm_sec = comp->start.tm_sec;
	tm_before.tm_min = comp->start.tm_min;
	tm_before.tm_hour = comp->start.tm_hour;
	tm_after.tm_sec = comp->end.tm_sec;
	tm_after.tm_min = comp->end.tm_min;
	tm_after.tm_hour = comp->end.tm_hour;
		
		
	before = mktime(&tm_before);
	after = mktime(&tm_after);
	now = mktime(tm_now);
	to_before = difftime(now,before);
	to_after = difftime(now,after);

	if(to_before > 0.0 && to_after < 0.0){
		return true;
	}
	return false;
}

/**
 * Checks for active timeranges for a given rule.
 * @param rule Pointer to a RULE object
 * @return true if the rule is active
 */
bool rule_is_active(RULE* rule)
{
	TIMERANGE* times;
	if(rule->active != NULL){

		times  = (TIMERANGE*)rule->active;
			
		while(times){

			if(inside_timerange(times)){
				return true;
			}

			times = times->next;
		}
		return false;
	}
	return true;
}

/**
 * Check if a query matches a single rule
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param rulelist The rule to check
 * @param query Pointer to the null-terminated query string
 * @return true if the query matches the rule
 */
bool rule_matches(FW_INSTANCE* my_instance, FW_SESSION* my_session, GWBUF *queue, USER* user, RULELIST *rulelist, char* query)
{
	char *ptr,*where,*msg = NULL;
	char emsg[512];
	int qlen;
	bool is_sql, is_real, matches;
	skygw_query_op_t optype = QUERY_OP_UNDEFINED;
	STRLINK* strln = NULL;
	QUERYSPEED* queryspeed = NULL;
	QUERYSPEED* rule_qs = NULL;
	time_t time_now;
	struct tm* tm_now; 

	if(my_session->errmsg){
		free(my_session->errmsg);
		my_session->errmsg = NULL;
	}

	time(&time_now);
	tm_now = localtime(&time_now);

	matches = false;
	is_sql = modutil_is_SQL(queue);
	
	if(is_sql){
		if(!query_is_parsed(queue)){
			parse_query(queue);
		}
		optype =  query_classifier_get_operation(queue);
		modutil_extract_SQL(queue, &ptr, &qlen);
		is_real = skygw_is_real_query(queue);
	}

	if(rulelist->rule->on_queries == QUERY_OP_UNDEFINED || rulelist->rule->on_queries & optype){

        switch(rulelist->rule->type){
			
        case RT_UNDEFINED:
            skygw_log_write_flush(LOGFILE_ERROR, "Error: Undefined rule type found.");	
            break;
			
        case RT_REGEX:

            if(query && regexec(rulelist->rule->data,query,0,NULL,0) == 0){

                matches = true;
				
                if(!rulelist->rule->allow){
                    msg = strdup("Permission denied, query matched regular expression.");
                    skygw_log_write(LOGFILE_TRACE, "fwfilter: rule '%s': regex matched on query",rulelist->rule->name);	
                    goto queryresolved;
                }else{
                    break;
                }
            }

            break;

        case RT_PERMISSION:
            if(!rulelist->rule->allow){
                matches = true;
                msg = strdup("Permission denied at this time.");
                skygw_log_write(LOGFILE_TRACE, "fwfilter: rule '%s': query denied at: %s",rulelist->rule->name,asctime(tm_now));	
                goto queryresolved;
            }else{
                break;
            }
            break;
			
        case RT_COLUMN:
		   
            if(is_sql && is_real){

                strln = (STRLINK*)rulelist->rule->data;			
                where = skygw_get_affected_fields(queue);

                if(where != NULL){

                    while(strln){
                        if(strstr(where,strln->value)){

                            matches = true;

                            if(!rulelist->rule->allow){
                                sprintf(emsg,"Permission denied to column '%s'.",strln->value);
                                skygw_log_write(LOGFILE_TRACE, "fwfilter: rule '%s': query targets forbidden column: %s",rulelist->rule->name,strln->value);	
                                msg = strdup(emsg);
                                goto queryresolved;
                            }else{
                                break;
                            }
                        }
                        strln = strln->next;
                    }
                }
            }
			
            break;

        case RT_WILDCARD:


            if(is_sql && is_real){
                char * strptr;
                where = skygw_get_affected_fields(queue);
						
                if(where != NULL){
                    strptr = where;
                }else{
                    strptr = query;
                }
                if(strchr(strptr,'*')){

                    matches = true;
                    msg = strdup("Usage of wildcard denied.");
                    skygw_log_write(LOGFILE_TRACE, "fwfilter: rule '%s': query contains a wildcard.",rulelist->rule->name);	
                    goto queryresolved;
                }
            }
			
            break;

        case RT_THROTTLE:
				
            /**
             * Check if this is the first time this rule is matched and if so, allocate
             * and initialize a new QUERYSPEED struct for this session.
             */
				
            spinlock_acquire(my_instance->lock);
            rule_qs = (QUERYSPEED*)rulelist->rule->data;
            spinlock_release(my_instance->lock);

            spinlock_acquire(user->lock);
            queryspeed = user->qs_limit;


            while(queryspeed){
                if(queryspeed->id == rule_qs->id){
                    break;
                }
                queryspeed = queryspeed->next;
            }

            if(queryspeed == NULL){

                /**No match found*/
                queryspeed = (QUERYSPEED*)calloc(1,sizeof(QUERYSPEED));
                queryspeed->period = rule_qs->period;
                queryspeed->cooldown = rule_qs->cooldown;
                queryspeed->limit = rule_qs->limit;
                queryspeed->id = rule_qs->id;
                queryspeed->next = user->qs_limit;
                user->qs_limit = queryspeed;
            }
				
            if(queryspeed->count > queryspeed->limit)
            {
                queryspeed->triggered = time_now;
                queryspeed->count = 0;
                matches = true;


                skygw_log_write(LOGFILE_TRACE, 
                                "fwfilter: rule '%s': query limit triggered (%d queries in %f seconds), denying queries from user for %f seconds.",
                                rulelist->rule->name,
                                queryspeed->limit,
                                queryspeed->period,
                                queryspeed->cooldown);
                double blocked_for = queryspeed->cooldown - difftime(time_now,queryspeed->triggered);
                sprintf(emsg,"Queries denied for %f seconds",blocked_for);
                msg = strdup(emsg);
            }
            else if(difftime(time_now,queryspeed->triggered) < queryspeed->cooldown)
            {

                double blocked_for = queryspeed->cooldown - difftime(time_now,queryspeed->triggered);

                sprintf(emsg,"Queries denied for %f seconds",blocked_for);
                skygw_log_write(LOGFILE_TRACE, "fwfilter: rule '%s': user denied for %f seconds",rulelist->rule->name,blocked_for);	
                msg = strdup(emsg);
					
                matches = true;				
            }
            else if(difftime(time_now,queryspeed->first_query) < queryspeed->period)
            {
                queryspeed->count++;
            }
            else
            {
                queryspeed->first_query = time_now;
            }
            spinlock_release(user->lock);
            break;

        case RT_CLAUSE:

            if(is_sql && is_real &&
               !skygw_query_has_clause(queue))
            {
                matches = true;
                msg = strdup("Required WHERE/HAVING clause is missing.");
                skygw_log_write(LOGFILE_TRACE, "fwfilter: rule '%s': query has no where/having clause, query is denied.",
                                rulelist->rule->name);
            }
            break;
	
        default:
            break;

        }
    }

    queryresolved:
	if(msg){
		my_session->errmsg = msg;
	}
	
	if(matches){
		rulelist->rule->times_matched++;
	}
	
	return matches;
}

/**
 * Check if the query matches any of the rules in the user's rulelist.
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param user The user whose rulelist is checked
 * @return True if the query matches at least one of the rules otherwise false
 */
bool check_match_any(FW_INSTANCE* my_instance, FW_SESSION* my_session, GWBUF *queue, USER* user)
{
	bool is_sql, rval = false;
	int qlen;
	char *fullquery = NULL,*ptr;
	
	RULELIST* rulelist;
	is_sql = modutil_is_SQL(queue);
	
	if(is_sql){
		if(!query_is_parsed(queue)){
			parse_query(queue);
		}
		modutil_extract_SQL(queue, &ptr, &qlen);
		fullquery = malloc((qlen + 1) * sizeof(char));
		memcpy(fullquery,ptr,qlen);
		memset(fullquery + qlen,0,1);
	}

	rulelist = user->rules_or;

	while(rulelist){
		
		if(!rule_is_active(rulelist->rule)){
			rulelist = rulelist->next;
			continue;
		}
	    if((rval = rule_matches(my_instance,my_session,queue,user,rulelist,fullquery))){
			goto retblock;
		}
		rulelist = rulelist->next;
	}

    retblock:

	free(fullquery);

	return rval;
}

/**
 * Check if the query matches all rules in the user's rulelist.
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param user The user whose rulelist is checked
 * @return True if the query matches all of the rules otherwise false
 */
bool check_match_all(FW_INSTANCE* my_instance, FW_SESSION* my_session, GWBUF *queue, USER* user)
{
	bool is_sql, rval = 0;
	int qlen;
	char *fullquery = NULL,*ptr;
	
	RULELIST* rulelist;
	is_sql = modutil_is_SQL(queue);
	
	if(is_sql){
		if(!query_is_parsed(queue)){
			parse_query(queue);
		}
		modutil_extract_SQL(queue, &ptr, &qlen);
		fullquery = malloc((qlen + 1) * sizeof(char));
		memcpy(fullquery,ptr,qlen);
		memset(fullquery + qlen,0,1);


	}

	rulelist = user->rules_or;

	while(rulelist){
		
		if(!rule_is_active(rulelist->rule)){
			rulelist = rulelist->next;
			continue;
		}

		if(!rule_matches(my_instance,my_session,queue,user,rulelist,fullquery)){
			rval = false;
			goto retblock;
		}
		rulelist = rulelist->next;
	}
	
    retblock:
	
	free(fullquery);
	
	return rval;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	FW_INSTANCE	*my_instance = (FW_INSTANCE *)instance;
	bool accept = my_instance->def_op;
	char  *msg = NULL, *fullquery = NULL,*ipaddr;
	char uname_addr[128];
    DCB* dcb = my_session->session->client;
	USER* user = NULL;
	GWBUF* forward;
	ipaddr = strdup(dcb->remote);
	sprintf(uname_addr,"%s@%s",dcb->user,ipaddr);
	
	
	if((user = (USER*)hashtable_fetch(my_instance->htable, uname_addr)) == NULL){
		while(user == NULL && next_ip_class(ipaddr)){
			sprintf(uname_addr,"%s@%s",dcb->user,ipaddr);
			user = (USER*)hashtable_fetch(my_instance->htable, uname_addr);
		}
	}
	
	if(user == NULL){
		strcpy(ipaddr,dcb->remote);
		
		do{
			sprintf(uname_addr,"%%@%s",ipaddr);
			user = (USER*)hashtable_fetch(my_instance->htable, uname_addr);
		}while(user == NULL && next_ip_class(ipaddr));			
	}
	
	if(user  == NULL){
		
		/** 
		 *No rules matched, do default operation.
		 */
		
		goto queryresolved;
	}

	if(check_match_any(my_instance,my_session,queue,user)){
		accept = false;
		goto queryresolved;
	}

	if(check_match_all(my_instance,my_session,queue,user)){
		accept = false;
		goto queryresolved;
	}
	
    queryresolved:

	free(ipaddr);
	free(fullquery);

	if(accept){

		return my_session->down.routeQuery(my_session->down.instance,
										   my_session->down.session, queue);
	}else{
	    
		gwbuf_free(queue);

		if(my_session->errmsg){
			msg = my_session->errmsg;	
		}
	    forward = gen_dummy_error(my_session,msg);

		if(my_session->errmsg){
			free(my_session->errmsg);
			my_session->errmsg = NULL;
		}
		return dcb->func.write(dcb,forward);
	}
}

/**
 * Diagnostics routine
 *
 * Prints the connection details and the names of the exchange,
 * queue and the routing key.
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
	FW_INSTANCE	*my_instance = (FW_INSTANCE *)instance;
    RULELIST* rules;
    int type;
	
	if (my_instance)
    {
        spinlock_acquire(my_instance->lock);
        rules = my_instance->rules;
			
        dcb_printf(dcb, "Firewall Filter\n");
        dcb_printf(dcb, "%-24s%-24s%-24s\n","Rule","Type","Times Matched");
        while(rules){
            if((int)rules->rule->type > 0 &&
               (int)rules->rule->type < sizeof(rule_names)/sizeof(char**)){
                type = (int)rules->rule->type;
            }else{
                type = 0;
            }
            dcb_printf(dcb,"%-24s%-24s%-24d\n",
                       rules->rule->name,
                       rule_names[type],
                       rules->rule->times_matched);
            rules = rules->next;
        }
        spinlock_release(my_instance->lock);
    }
}
