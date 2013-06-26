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
#include <string.h>
#include <skygw_utils.h>
#include <log_manager.h>

int main(int argc, char* argv[])
{
        int           err;
        logmanager_t* lmgr;
        char*         logstr;
        
        lmgr = skygw_logmanager_init(NULL, argc, argv);
        
        logstr = strdup("My name is Tracey");
        err = skygw_log_write(NULL, lmgr, LOGFILE_TRACE, logstr);
        
        logstr = strdup("My name is Stacey");
        err = skygw_log_write_flush(NULL, lmgr, LOGFILE_TRACE, logstr);
        
        skygw_logmanager_done(NULL, &lmgr);

        logstr = strdup("My name is Philip");
        err = skygw_log_write(NULL, lmgr, LOGFILE_TRACE, logstr);
        
        lmgr = skygw_logmanager_init(NULL, argc, argv);
        
        logstr = strdup("A terrible error has occurred!");
        err = skygw_log_write_flush(NULL, lmgr, LOGFILE_ERROR, logstr);

        logstr = strdup("Hi, how are you?");
        err = skygw_log_write(NULL, lmgr, LOGFILE_MESSAGE, logstr);

        logstr = strdup("I'm doing fine!");
        err = skygw_log_write(NULL, lmgr, LOGFILE_MESSAGE, logstr);

        logstr = strdup("I was wondering, you know, it has been such a lovely weather whole morning and I thought that would you like to come to my place and have a little piece of cheese with us. Just me and my mom - and you, of course. Then, if you wish, we could listen to the radio and keep company for our little Steven, my mom's cat, you see.");
        err = skygw_log_write(NULL, lmgr, LOGFILE_MESSAGE, logstr);
        
return_err:
        skygw_logmanager_done(NULL, &lmgr);
        return err;
}
