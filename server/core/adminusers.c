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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include <unistd.h>
#include <crypt.h>
#include <users.h>
#include <adminusers.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <gwdirs.h>
#include <sys/stat.h>

/**
 * @file adminusers.c - Administration user account management
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 18/07/13     Mark Riddoch    Initial implementation
 * 23/07/13     Mark Riddoch    Addition of error mechanism to add user
 *
 * @endverbatim
 */
static USERS *loadUsers();
static void  initialise();

static USERS *users = NULL;
static int   admin_init = 0;

static char *ADMIN_ERR_NOMEM            = "Out of memory";
static char *ADMIN_ERR_FILEOPEN         = "Unable to create password file";
static char *ADMIN_ERR_DUPLICATE        = "Duplicate username specified";
static char *ADMIN_ERR_USERNOTFOUND     = "User not found";
static char *ADMIN_ERR_AUTHENTICATION   = "Authentication failed";
static char *ADMIN_ERR_FILEAPPEND       = "Unable to append to password file";
static char *ADMIN_ERR_PWDFILEOPEN      = "Failed to open password file";
static char *ADMIN_ERR_TMPFILEOPEN      = "Failed to open temporary password file";
static char *ADMIN_ERR_PWDFILEACCESS    = "Failed to access password file";
static char *ADMIN_ERR_DELLASTUSER      = "Deleting the last user is forbidden";
static char *ADMIN_SUCCESS              = NULL;

static const int LINELEN=80;

/**
 * Admin Users initialisation
 */
static void
initialise()
{
    if (admin_init)
    {
        return;
    }

    admin_init = 1;
    users = loadUsers();
}

/**
 * Verify a username and password
 *
 * @param username      Username to verify
 * @param password      Password to verify
 * @return Non-zero if the username/password combination is valid
 */
int
admin_verify(char *username, char *password)
{
    char *pw;

    initialise();
    if (users == NULL)
    {
        if (strcmp(username, "admin") == 0 && strcmp(password, "mariadb") == 0)
        {
            return 1;
        }
    }
    else
    {
        if ((pw = users_fetch(users, username)) == NULL)
        {
            return 0;
        }
        if (strcmp(pw, crypt(password, ADMIN_SALT)) == 0)
        {
            return 1;
        }
    }
    return 0;
}


/**
 * Load the admin users
 *
 * @return Table of users
 */
static USERS *
loadUsers()
{
    USERS *rval;
    FILE  *fp;
    char  fname[1024], *home;
    char  uname[80], passwd[80];

    initialise();
    snprintf(fname,1023, "%s/passwd", get_datadir());
    fname[1023] = '\0';
    if ((fp = fopen(fname, "r")) == NULL)
    {
        return NULL;
    }
    if ((rval = users_alloc()) == NULL)
    {
        fclose(fp);
        return NULL;
    }
    while (fscanf(fp, "%[^:]:%s\n", uname, passwd) == 2)
    {
        users_add(rval, uname, passwd);
    }
    fclose(fp);

    return rval;
}

/**
 * Add user
 *
 * @param uname         Name of the new user
 * @param passwd        Password for the new user
 * @return      NULL on success or an error string on failure
 */
char *
admin_add_user(char *uname, char *passwd)
{
    FILE *fp;
    char fname[1024], *home, *cpasswd;

    initialise();

    if (access(get_datadir(), F_OK) != 0)
    {
        if (mkdir(get_datadir(), S_IRWXU) != 0 && errno != EEXIST)
        {
            return ADMIN_ERR_PWDFILEOPEN;
        }
    }

    snprintf(fname,1023, "%s/passwd", get_datadir());
    fname[1023] = '\0';
    if (users == NULL)
    {
        MXS_NOTICE("Create initial password file.");

        if ((users = users_alloc()) == NULL)
        {
            return ADMIN_ERR_NOMEM;
        }
        if ((fp = fopen(fname, "w")) == NULL)
        {
            MXS_ERROR("Unable to create password file %s.", fname);
            return ADMIN_ERR_PWDFILEOPEN;
        }
        fclose(fp);
    }
    if (users_fetch(users, uname) != NULL)
    {
        return ADMIN_ERR_DUPLICATE;
    }
    cpasswd = crypt(passwd, ADMIN_SALT);
    users_add(users, uname, cpasswd);
    if ((fp = fopen(fname, "a")) == NULL)
    {
        MXS_ERROR("Unable to append to password file %s.", fname);
        return ADMIN_ERR_FILEAPPEND;
    }
    fprintf(fp, "%s:%s\n", uname, cpasswd);
    fclose(fp);
    return ADMIN_SUCCESS;
}


/**
 * Remove maxscale user from in-memory structure and from password file
 *
 * @param uname         Name of the new user
 * @param passwd        Password for the new user
 * @return      NULL on success or an error string on failure
 */
char* admin_remove_user(
    char* uname,
    char* passwd)
{
    FILE*  fp;
    FILE*  fp_tmp;
    char   fname[1024];
    char   fname_tmp[1024];
    char*  home;
    char   fusr[LINELEN];
    char   fpwd[LINELEN];
    char   line[LINELEN];
    fpos_t rpos;
    int    n_deleted;

    if (!admin_search_user(uname))
    {
        MXS_ERROR("Couldn't find user %s. Removing user failed.", uname);
        return ADMIN_ERR_USERNOTFOUND;
    }

    if (admin_verify(uname, passwd) == 0)
    {
        MXS_ERROR("Authentication failed, wrong user/password "
                  "combination. Removing user failed.");
        return ADMIN_ERR_AUTHENTICATION;
    }


    /** Remove user from in-memory structure */
    n_deleted = users_delete(users, uname);

    if (n_deleted == 0)
    {
        MXS_ERROR("Deleting the only user is forbidden. Add new "
                  "user before deleting the one.");
        return ADMIN_ERR_DELLASTUSER;
    }
    /**
     * Open passwd file and remove user from the file.
     */
    snprintf(fname, 1023, "%s/passwd", get_datadir());
    snprintf(fname_tmp, 1023, "%s/passwd_tmp", get_datadir());
    fname[1023] = '\0';
    fname_tmp[1023] = '\0';
    /**
     * Rewrite passwd file from memory.
     */
    if ((fp = fopen(fname, "r")) == NULL)
    {
        int err = errno;
        MXS_ERROR("Unable to open password file %s : errno %d.\n"
                  "Removing user from file failed; it must be done "
                  "manually.",
                  fname,
                  err);
        return ADMIN_ERR_PWDFILEOPEN;
    }
    /**
     * Open temporary passwd file.
     */
    if ((fp_tmp = fopen(fname_tmp, "w")) == NULL)
    {
        int err = errno;
        MXS_ERROR("Unable to open tmp file %s : errno %d.\n"
                  "Removing user from passwd file failed; it must be done "
                  "manually.",
                  fname_tmp,
                  err);
        fclose(fp);
        return ADMIN_ERR_TMPFILEOPEN;
    }

    /**
     * Scan passwd and copy all but matching lines to temp file.
     */
    if (fgetpos(fp, &rpos) != 0)
    {
        int err = errno;
        MXS_ERROR("Unable to process passwd file %s : errno %d.\n"
                  "Removing user from file failed, and must be done "
                  "manually.",
                  fname,
                  err);
        fclose(fp);
        fclose(fp_tmp);
        unlink(fname_tmp);
        return ADMIN_ERR_PWDFILEACCESS;
    }

    while (fscanf(fp, "%[^:]:%s\n", fusr, fpwd) == 2)
    {
        /**
         * Compare username what was found from passwd file.
         * Unmatching lines are copied to tmp file.
         */
        if (strncmp(uname, fusr, strlen(uname)+1) != 0)
        {
            if(fsetpos(fp, &rpos) != 0)
            { /** one step back */
                MXS_ERROR("Unable to set stream position. ");
            }
            fgets(line, LINELEN, fp);
            fputs(line, fp_tmp);
        }

        if (fgetpos(fp, &rpos) != 0)
        {
            int err = errno;
            MXS_ERROR("Unable to process passwd file %s : "
                      "errno %d.\n"
                      "Removing user from file failed, and must be "
                      "done manually.",
                      fname,
                      err);
            fclose(fp);
            fclose(fp_tmp);
            unlink(fname_tmp);
            return ADMIN_ERR_PWDFILEACCESS;
        }
    }
    fclose(fp);
    /**
     * Replace original passwd file with new.
     */
    if (rename(fname_tmp, fname))
    {
        int err = errno;
        MXS_ERROR("Unable to rename new passwd file %s : errno "
                  "%d.\n"
                  "Rename it to %s manually.",
                  fname_tmp,
                  err,
                  fname);
        unlink(fname_tmp);
        fclose(fp_tmp);
        return ADMIN_ERR_PWDFILEACCESS;
    }
    fclose(fp_tmp);
    return ADMIN_SUCCESS;
}



/**
 * Check for existance of the user
 *
 * @param user  The user name to test
 * @return      Non-zero if the user exists
 */
int
admin_search_user(char *user)
{
    initialise();
    if (users == NULL)
    {
        return 0;
    }
    return users_fetch(users, user) != NULL;
}

/**
 * Print the statistics and user names of the administration users
 *
 * @param dcb   A DCB to send the output to
 */
void
dcb_PrintAdminUsers(DCB *dcb)
{
    if (users)
    {
        dcb_usersPrint(dcb, users);
    }
    else
    {
        dcb_printf(dcb, "No administration users have been defined.\n");
    }
}
