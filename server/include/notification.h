#ifndef _NOTIFICATION_SERVICE_H
#define _NOTIFICATION_SERVICE_H
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

/**
 * @file notification.h
 *
 * The configuration stuct for notification/feedback service
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 02/03/15	Massimiliano Pinto	Initial implementation
 *
 * @endverbatim
 */

#define _NOTIFICATION_CONNECT_TIMEOUT 	30
#define _NOTIFICATION_OPERATION_TIMEOUT 30
#define _NOTIFICATION_SEND_PENDING	0
#define _NOTIFICATION_SEND_OK		1
#define _NOTIFICATION_SEND_ERROR 	2
#define	_NOTIFICATION_REPORT_ROW_LEN	255

#include <stdint.h>

/**
 * The configuration and usage information data for feeback service
 */

typedef struct {
	int	feedback_enable;		/**< Enable/Disable Notification feedback */
	char	*feedback_url;			/**< URL to which the data is sent */
	char	*feedback_user_info;		/**< User info included in the feedback data sent */
	int	feedback_timeout;		/**< An attempt to write/read the data times out and fails after this many seconds */
	int	feedback_connect_timeout;	/**< An attempt to send the data times out and fails after this many seconds */
	int	feedback_last_action;		/**< Holds the feedback last send action status */
        int     feedback_frequency;             /*< Frequency of the housekeeper task */
        char	*release_info;			/**< Operating system Release name */
        char	*sysname;			/**< Operating system name */
        uint8_t	*mac_sha1;			/**< First available MAC address*/
} FEEDBACK_CONF;

extern char  *gw_bin2hex(char *out, const uint8_t *in, unsigned int len);
extern void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out);
extern FEEDBACK_CONF * config_get_feedback_data();
#endif
