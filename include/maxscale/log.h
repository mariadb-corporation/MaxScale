/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/cdefs.h>

#include <assert.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>

#include <maxbase/log.h>
#include <maxbase/string.hh>

MXS_BEGIN_DECLS

#if !defined (MXS_MODULE_NAME)
#define MXS_MODULE_NAME NULL
#endif

#if !defined (MXB_MODULE_NAME)
#define MXB_MODULE_NAME MXS_MODULE_NAME
#endif

typedef mxb_log_target_t mxs_log_target_t;
#define MXS_LOG_TARGET_DEFAULT MXB_LOG_TARGET_DEFAULT
#define MXS_LOG_TARGET_FS      MXB_LOG_TARGET_FS
#define MXS_LOG_TARGET_STDOUT  MXB_LOG_TARGET_STDOUT

typedef MXB_LOG_THROTTLING MXS_LOG_THROTTLING;

/**
 * Initializes MaxScale log manager
 *
 * @param ident  The syslog ident. If NULL, then the program name is used.
 * @param logdir The directory for the log file. If NULL, file output is discarded.
 * @param target Logging target
 *
 * @return true if succeed, otherwise false
 */
bool mxs_log_init(const char* ident, const char* logdir, mxs_log_target_t target);

#define mxs_log_finish  mxb_log_finish
#define mxs_log_message mxb_log_message
#define mxs_log_rotate  mxb_log_rotate

#define mxs_log_get_throttling            mxb_log_get_throttling
#define mxs_log_is_priority_enabled       mxb_log_is_priority_enabled
#define mxs_log_set_augmentation          mxb_log_set_augmentation
#define mxs_log_set_highprecision_enabled mxb_log_set_highprecision_enabled
#define mxs_log_set_maxlog_enabled        mxb_log_set_maxlog_enabled
#define mxs_log_set_highprecision_enabled mxb_log_set_highprecision_enabled
#define mxs_log_set_priority_enabled      mxb_log_set_priority_enabled
#define mxs_log_set_syslog_enabled        mxb_log_set_syslog_enabled
#define mxs_log_set_throttling            mxb_log_set_throttling

json_t* mxs_logs_to_json(const char* host);

#define MXS_LOG_MESSAGE MXB_LOG_MESSAGE

#define MXS_ALERT   MXB_ALERT
#define MXS_ERROR   MXB_ERROR
#define MXS_WARNING MXB_WARNING
#define MXS_NOTICE  MXB_NOTICE
#define MXS_INFO    MXB_INFO
#define MXS_DEBUG   MXB_DEBUG

#define MXS_OOM_MESSAGE        MXB_OOM_MESSAGE
#define MXS_OOM_MESSAGE_IFNULL MXB_OOM_MESSAGE_IFNULL
#define MXS_OOM                MXB_OOM
#define MXS_OOM_IFNULL         MXB_OOM_IFNULL

#define mxs_strerror mxb_strerror

MXS_END_DECLS
