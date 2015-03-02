#ifndef _NOTIFICATION_H
#define _NOTIFICATION_H
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

/**
 * The configuration and usage information data for feeback service
 */

typedef struct {
	int	feedback_enable;		/**< Enable/Disable Notification feedback */
	char	*feedback_url;			/**< URL to which the data is sent */
	char	*feedback_setup_info;		/**< MaxScale setup identifier info included in the feedback data sent */
	char	*feedback_user_info;		/**< User info included in the feedback data sent */
	int	feedback_timeout;		/**< An attempt to write/read the data times out and fails after this many seconds */
	int	feedback_connect_timeout;	/**< An attempt to send the data times out and fails after this many seconds */
	int	feedback_last_action;		/**< Holds the feedback last send action status */
} FEEDBACK_CONF;


extern FEEDBACK_CONF*   notification_get_config_feedback();

#endif
