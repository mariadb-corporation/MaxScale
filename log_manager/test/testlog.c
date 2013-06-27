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


/** @file 
@brief (brief description)

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <skygw_utils.h>
#include <log_manager.h>

typedef struct thread_st {
        skygw_message_t* mes;
        simple_mutex_t*  mtx;
        size_t*          nactive;
} thread_t;

static void* thr_run(void* data);

#define NTHR 256

int main(int argc, char* argv[])
{
        int           err;
        char*         logstr;
        
        int              i;
        bool             r;
        skygw_message_t* mes;
        simple_mutex_t*  mtx;
        size_t           nactive;
        thread_t*        thr[NTHR];
                
        r = skygw_logmanager_init(NULL, argc, argv);
        ss_dassert(r);
        logstr = strdup("My name is Tracey");
        err = skygw_log_write(NULL, LOGFILE_TRACE, logstr);
        
        logstr = strdup("My name is Stacey");
        err = skygw_log_write_flush(NULL, LOGFILE_TRACE, logstr);
        
        skygw_logmanager_done(NULL);

        logstr = strdup("My name is Philip");
        err = skygw_log_write(NULL, LOGFILE_TRACE, logstr);
        
        skygw_logmanager_init(NULL, argc, argv);
        
        logstr = strdup("A terrible error has occurred!");
        err = skygw_log_write_flush(NULL, LOGFILE_ERROR, logstr);

        logstr = strdup("Hi, how are you?");
        err = skygw_log_write(NULL, LOGFILE_MESSAGE, logstr);

        logstr = strdup("I'm doing fine!");
        err = skygw_log_write(NULL, LOGFILE_MESSAGE, logstr);

        logstr = strdup("Rather more surprising, at least at first sight, is the fact that a reference to a[i] can also be written as *(a+i). In evaluating a[i], C converts it to *(a+i) immediately; the two forms are equivalent. Applying the operators & to both parts of this equivalence, it follows that &a[i] and a+i are also identical: a+i is the address of the i-th element beyond a.");
        err = skygw_log_write(NULL, LOGFILE_ERROR, logstr);

        logstr = strdup("I was wondering, you know, it has been such a lovely weather whole morning and I thought that would you like to come to my place and have a little piece of cheese with us. Just me and my mom - and you, of course. Then, if you wish, we could listen to the radio and keep company for our little Steven, my mom's cat, you know.");
        err = skygw_log_write(NULL, LOGFILE_MESSAGE, logstr);
        skygw_logmanager_done(NULL);

        mes = skygw_message_init();
        mtx = simple_mutex_init(NULL, strdup("testmtx"));
        
        for (i=0; i<NTHR; i++) {
            thr[i] = (thread_t*)calloc(1, sizeof(thread_t));
            thr[i]->mes = mes;
            thr[i]->mtx = mtx;
            thr[i]->nactive = &nactive;
        }
        nactive = NTHR;

        for (i=0; i<NTHR; i++)  {
            pthread_t p;
            pthread_create(&p, NULL, thr_run, thr[i]);
        }

        do {
            skygw_message_wait(mes);
            simple_mutex_lock(mtx, TRUE);
            if (nactive > 0) {
                simple_mutex_unlock(mtx);
                continue;
            }
            break;
        } while(TRUE);
        /** This is to release memory */
        skygw_logmanager_done(NULL);
        
        simple_mutex_unlock(mtx);
        
        for (i=0; i<NTHR; i++) {
            free(thr[i]);
        }
        skygw_message_done(mes);
        simple_mutex_done(mtx);
        return err;
}


void* thr_run(
        void* data)
{
        thread_t* td = (thread_t *)data;
        char*     logstr;
        int       err;

        skygw_logmanager_init(NULL, 0, NULL);
        skygw_logmanager_done(NULL);
        skygw_log_flush(LOGFILE_MESSAGE);
        logstr = strdup("Hi, how are you?");
        err = skygw_log_write(NULL, LOGFILE_MESSAGE, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_done(NULL);
        skygw_log_flush(LOGFILE_TRACE);
        skygw_log_flush(LOGFILE_MESSAGE);
        logstr = strdup("I was wondering, you know, it has been such a lovely weather whole morning and I thought that would you like to come to my place and have a little piece of cheese with us. Just me and my mom - and you, of course. Then, if you wish, we could listen to the radio and keep company for our little Steven, my mom's cat, you know.");
        ss_dassert(err == 0);
        err = skygw_log_write(NULL, LOGFILE_MESSAGE, logstr);
        skygw_logmanager_init(NULL, 0, NULL);
        logstr = strdup("Testing. One, two, three\n");
        err = skygw_log_write(NULL, LOGFILE_ERROR, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_init(NULL, 0, NULL);
        skygw_logmanager_init(NULL, 0, NULL);
        skygw_log_flush(LOGFILE_ERROR);
        logstr = strdup("For automatic and register variables, it is done each time the function or block is entered.");
        err = skygw_log_write(NULL, LOGFILE_TRACE, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_done(NULL);
        skygw_logmanager_init(NULL, 0, NULL);
        logstr = strdup("Rather more surprising, at least at first sight, is the fact that a reference to a[i] can also be written as *(a+i). In evaluating a[i], C converts it to *(a+i) immediately; the two forms are equivalent. Applying the operatos & to both parts of this equivalence, it follows that &a[i] and a+i are also identical: a+i is the address of the i-th element beyond a.");
        err = skygw_log_write(NULL, LOGFILE_ERROR, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_init(NULL, 0, NULL);
        skygw_logmanager_done(NULL);
        skygw_log_flush(LOGFILE_ERROR);
        skygw_logmanager_done(NULL);
        skygw_logmanager_done(NULL);
        logstr = strdup("..and you?");
        err = skygw_log_write(NULL, LOGFILE_MESSAGE, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_init(NULL, 0, NULL);
        skygw_logmanager_init(NULL, 0, NULL);
        logstr = strdup("For automatic and register variables, it is done each time the function or block is entered.");
        err = skygw_log_write(NULL, LOGFILE_TRACE, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_init(NULL, 0, NULL);
        logstr = strdup("Rather more surprising, at least at first sight, is the fact that a reference to a[i] can also be written as *(a+i). In evaluating a[i], C converts it to *(a+i) immediately; the two forms are equivalent. Applying the operatos & to both parts of this equivalence, it follows that &a[i] and a+i are also identical: a+i is the address of the i-th element beyond a.");
        err = skygw_log_write(NULL, LOGFILE_ERROR, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_init(NULL, 0, NULL);
        logstr = strdup("..... and you too?");
        err = skygw_log_write(NULL, LOGFILE_MESSAGE, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_done(NULL);
        skygw_log_flush(LOGFILE_TRACE);
        logstr = strdup("For automatic and register variables, it is done each time the function or block is entered.");
        err = skygw_log_write(NULL, LOGFILE_TRACE, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_done(NULL);
        logstr = strdup("Testing. One, two, three, four\n");
        err = skygw_log_write(NULL, LOGFILE_ERROR, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_init(NULL, 0, NULL);
        logstr = strdup("Testing. One, two, three, .. where was I?\n");
        err = skygw_log_write(NULL, LOGFILE_ERROR, logstr);
        ss_dassert(err == 0);
        skygw_logmanager_init(NULL, 0, NULL);
        skygw_logmanager_init(NULL, 0, NULL);
        skygw_logmanager_done(NULL);
        simple_mutex_lock(td->mtx, TRUE);
        *td->nactive -= 1;
        simple_mutex_unlock(td->mtx);
        skygw_message_send(td->mes);
        return NULL;
}
