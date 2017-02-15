#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file notification.h
 *
 * The configuration stuct for notification/feedback service
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

#define _NOTIFICATION_CONNECT_TIMEOUT   30
#define _NOTIFICATION_OPERATION_TIMEOUT 30
#define _NOTIFICATION_SEND_PENDING      0
#define _NOTIFICATION_SEND_OK           1
#define _NOTIFICATION_SEND_ERROR        2
#define _NOTIFICATION_REPORT_ROW_LEN    255

#include <stdint.h>

/**
 * The configuration and usage information data for feeback service
 */

typedef struct
{
    int feedback_enable;         /**< Enable/Disable Notification feedback */
    char *feedback_url;          /**< URL to which the data is sent */
    char *feedback_user_info;    /**< User info included in the feedback data sent */
    int feedback_timeout;        /**< An attempt to write/read the data times out and fails after this many seconds */
    int feedback_connect_timeout;/**< An attempt to send the data times out and fails after this many seconds */
    int feedback_last_action;    /**< Holds the feedback last send action status */
    int feedback_frequency;      /*< Frequency of the housekeeper task */
    char *release_info;          /**< Operating system Release name */
    char *sysname;               /**< Operating system name */
    uint8_t *mac_sha1;           /**< First available MAC address*/
} FEEDBACK_CONF;

extern char  *gw_bin2hex(char *out, const uint8_t *in, unsigned int len);
extern void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out);
extern FEEDBACK_CONF * config_get_feedback_data();

MXS_END_DECLS
