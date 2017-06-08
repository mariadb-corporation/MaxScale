/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#ifndef SS_DEBUG
#define SS_DEBUG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/alloc.h>
#include "../maxscale/skygw_utils.h"
#include <maxscale/log_manager.h>

static void skygw_log_enable(int priority)
{
    mxs_log_set_priority_enabled(priority, true);
}

static void skygw_log_disable(int priority)
{
    mxs_log_set_priority_enabled(priority, false);
}

int main(int argc, char* argv[])
{
    int              err = 0;
    const char*            logstr;

    int              i;
    bool             succp;
    skygw_message_t* mes;
    simple_mutex_t*  mtx;
    size_t           nactive;
    time_t           t;
    struct tm        tm;
    char             c;

    succp = mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);

    ss_info_dassert(succp, "Log manager initialization failed");

    t = time(NULL);
    localtime_r(&t, &tm);
    err = MXS_ERROR("%04d %02d/%02d %02d.%02d.%02d",
                    tm.tm_year + 1900,
                    tm.tm_mon + 1,
                    tm.tm_mday,
                    tm.tm_hour,
                    tm.tm_min,
                    tm.tm_sec);

    logstr = ("First write with flush.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("Second write with flush.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("Third write, no flush.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("Fourth write, no flush. Next flush only.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    err = mxs_log_flush();
    ss_dassert(err == 0);

    logstr = "My name is %s %d years and %d months.";
    err = MXS_INFO(logstr, "TraceyTracey", 3, 7);
    ss_dassert(err == 0);

    err = mxs_log_flush();
    ss_dassert(err == 0);

    logstr = "My name is Tracey Tracey 47 years and 7 months.";
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);

    logstr = "My name is Stacey %s";
    err = MXS_INFO(logstr, "           ");
    ss_dassert(err == 0);

    logstr = "My name is Philip";
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);

    logstr = "Philip.";
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);

    logstr = "Ph%dlip.";
    err = MXS_INFO(logstr, 1);
    ss_dassert(err == 0);

    logstr = ("A terrible error has occurred!");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("Hi, how are you?");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("I'm doing fine!");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("Rather more surprising, at least at first sight, is the fact that a reference to "
              "a[i] can also be written as *(a+i). In evaluating a[i], C converts it to *(a+i) "
              "immediately; the two forms are equivalent. Applying the operators & to both parts "
              "of this equivalence, it follows that &a[i] and a+i are also identical: a+i is the "
              "address of the i-th element beyond a.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("I was wondering, you know, it has been such a lovely weather whole morning and I "
              "thought that would you like to come to my place and have a little piece of cheese "
              "with us. Just me and my mom - and you, of course. Then, if you wish, we could "
              "listen to the radio and keep company for our little Steven, my mom's cat, you know.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("\tTEST 3 - test enabling and disabling logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_INFO);

    logstr = ("1.\tWrite once to ERROR and twice to MESSAGE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_INFO);

    logstr = ("2.\tWrite to once to ERROR, twice to MESSAGE and "
              "three times to TRACE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_ERR);

    logstr = ("3.\tWrite to once to MESSAGE and twice to TRACE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_NOTICE);
    skygw_log_disable(LOG_INFO);

    logstr = ("4.\tWrite to none.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_ERR);
    skygw_log_enable(LOG_NOTICE);

    logstr = ("4.\tWrite once to ERROR and twice to MESSAGE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);


    skygw_log_enable(LOG_INFO);
    logstr = ("\tTEST 4 - test spreading logs down to other logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("1.\tWrite to ERROR and thus also to MESSAGE and TRACE logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("2.\tWrite to MESSAGE and thus to TRACE logs.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_INFO);
    logstr = ("3.\tWrite to TRACE log only.");
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_NOTICE);

    logstr = ("4.\tWrite to ERROR and thus also to TRACE log. "
              "MESSAGE is disabled.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("5.\tThis should not appear anywhere since MESSAGE "
              "is disabled.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    succp = mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    ss_dassert(succp);
    skygw_log_enable(LOG_INFO);
    logstr = ("6.\tWrite to ERROR and thus also to MESSAGE and TRACE logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("7.\tWrite to MESSAGE and thus to TRACE logs.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("8.\tWrite to TRACE log only.");
    skygw_log_enable(LOG_INFO);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_NOTICE);

    logstr = ("9.\tWrite to ERROR and thus also to TRACE log. "
              "MESSAGE is disabled");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("10.\tThis should not appear anywhere since MESSAGE is "
              "disabled.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_NOTICE);

    err = MXS_ERROR("11.\tWrite to all logs some formattings : "
                    "%d %s %d",
                    (int)3,
                    "foo",
                    (int)3);
    ss_dassert(err == 0);
    err = MXS_ERROR("12.\tWrite to MESSAGE and TRACE log some "
                    "formattings "
                    ": %d %s %d",
                    (int)3,
                    "foo",
                    (int)3);
    ss_dassert(err == 0);
    err = MXS_ERROR("13.\tWrite to TRACE log some formattings "
                    ": %d %s %d",
                    (int)3,
                    "foo",
                    (int)3);
    ss_dassert(err == 0);

    mxs_log_finish();

    fprintf(stderr, ".. done.\n");

    return err;
}
