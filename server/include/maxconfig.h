#ifndef _MAXSCALE_CONFIG_H
#define _MAXSCALE_CONFIG_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */
#include <skygw_utils.h>
#include <stdint.h>
#include <openssl/sha.h>
#include <spinlock.h>
/**
 * @file config.h The configuration handling elements
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 21/06/13	Mark Riddoch		Initial implementation
 * 07/05/14	Massimiliano Pinto	Added version_string to global configuration
 * 23/05/14	Massimiliano Pinto	Added id to global configuration
 * 17/10/14	Mark Riddoch		Added poll tuning configuration parameters
 * 05/03/15	Massimiliano Pinto	Added sysname, release, sha1_mac to gateway struct
 *
 * @endverbatim
 */

#define		DEFAULT_NBPOLLS		3	/**< Default number of non block polls before we block */
#define		DEFAULT_POLLSLEEP	1000	/**< Default poll wait time (milliseconds) */
#define		_SYSNAME_STR_LENGTH	256	/**< sysname len */
#define		_RELEASE_STR_LENGTH	256	/**< release len */
/**
 * Maximum length for configuration parameter value.
 */
enum {MAX_PARAM_LEN=256};

typedef enum {
        UNDEFINED_TYPE     = 0x00,
        STRING_TYPE        = 0x01,
        COUNT_TYPE         = 0x02,
        PERCENT_TYPE       = 0x04,
        BOOL_TYPE          = 0x08,
	SQLVAR_TARGET_TYPE = 0x10
} config_param_type_t;

typedef enum {
	TYPE_UNDEFINED = 0,
	TYPE_MASTER,
	TYPE_ALL
} target_t;

enum {MAX_RLAG_NOT_AVAILABLE=-1, MAX_RLAG_UNDEFINED=-2};

#define PARAM_IS_TYPE(p,t) ((p) & (t))

/**
 * The config parameter
 */
typedef struct config_parameter {
	char			*name;		/**< The name of the parameter */
	char                    *value;         /**< The value of the parameter */
	union {                                 /*< qualified parameter value by type */
                char*           valstr;         /*< terminated char* array */
                int             valcount;       /*< int */
                int             valpercent;     /*< int */
                bool            valbool;        /*< bool */
                target_t        valtarget;      /*< sql variable route target */
        } qfd;
        config_param_type_t     qfd_param_type; 
	struct config_parameter	*next;		/**< Next pointer in the linked list */
} CONFIG_PARAMETER;

/**
 * The config context structure, used to build the configuration
 * data during the parse process
 */
typedef struct	config_context {
	char			*object;	/**< The name of the object being configured */
	CONFIG_PARAMETER	*parameters;	/**< The list of parameter values */
	void			*element;	/**< The element created from the data */
	struct config_context	*next;		/**< Next pointer in the linked list */
} CONFIG_CONTEXT;

/**
 * The gateway global configuration data
 */
typedef struct {
	int			n_threads;				/**< Number of polling threads */
	char			*version_string;			/**< The version string of embedded database library */
	char			release_string[_SYSNAME_STR_LENGTH];	/**< The release name string of the system */
	char			sysname[_SYSNAME_STR_LENGTH];		/**< The release name string of the system */
	uint8_t			mac_sha1[SHA_DIGEST_LENGTH];		/*< The SHA1 digest of an interface MAC address */
	unsigned long		id;					/**< MaxScale ID */
	unsigned int		n_nbpoll;		/**< Tune number of non-blocking polls */
	unsigned int		pollsleep;		/**< Wait time in blocking polls */
        int syslog; /*< Log to syslog */
        int maxlog; /*< Log to MaxScale's own logs */
        int log_to_shm; /*< Write log-file to shared memory */
        unsigned int auth_conn_timeout; /*< Connection timeout for the user authentication */
        unsigned int auth_read_timeout; /*< Read timeout for the user authentication */
        unsigned int auth_write_timeout; /*<  Write timeout for the user authentication */
} GATEWAY_CONF;

extern int		config_load(char *);
extern int		config_reload();
extern int		config_threadcount();
extern unsigned int	config_nbpolls();
extern unsigned int	config_pollsleep();
CONFIG_PARAMETER*	config_get_param(CONFIG_PARAMETER* params, const char* name);
config_param_type_t 	config_get_paramtype(CONFIG_PARAMETER* param);
CONFIG_PARAMETER*	config_clone_param(CONFIG_PARAMETER* param);
void			free_config_parameter(CONFIG_PARAMETER* p1);
extern int		config_truth_value(char *);
extern double           config_percentage_value(char *str);
bool config_set_qualified_param(
        CONFIG_PARAMETER* param, 
        void* val, 
        config_param_type_t type);


bool config_get_valint(
	int*                val,
        CONFIG_PARAMETER*   param,
        const char*         name, /*< if NULL examine current param only */
        config_param_type_t ptype);

bool config_get_valbool(
	bool*               val,
	CONFIG_PARAMETER*   param,
	const char*         name, /*< if NULL examine current param only */
	config_param_type_t ptype);

bool config_get_valtarget(
	target_t*           val,
	CONFIG_PARAMETER*   param,
	const char*         name, /*< if NULL examine current param only */
	config_param_type_t ptype);

void config_enable_feedback_task(void);
void config_disable_feedback_task(void);
unsigned long  config_get_gateway_id(void);
GATEWAY_CONF* config_get_global_options();
bool isInternalService(char *router);
char* config_clean_string_list(char* str);
#endif
