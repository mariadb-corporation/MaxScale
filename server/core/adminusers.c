/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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
 * Date         Who                  Description
 * 18/07/13     Mark Riddoch         Initial implementation
 * 23/07/13     Mark Riddoch         Addition of error mechanism to add user
 * 23/05/16     Massimiliano Pinto   admin_add_user and admin_remove_user
 *                                   no longer accept password parameter
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
static char *ADMIN_ERR_DELROOT          = "Deleting the default admin user is forbidden";
static char *ADMIN_SUCCESS              = NULL;

static const int LINELEN = 80;

static const char USERS_FILE_NAME[] = "maxadmin-users";

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
        struct crypt_data cdata;
        cdata.initialized = 0;
        if (strcmp(pw, crypt_r(password, ADMIN_SALT, &cdata)) == 0)
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
    char  fname[PATH_MAX], *home;
    char  uname[80];
    int added_users = 0;

    initialise();
    snprintf(fname, sizeof(fname), "%s/%s", get_datadir(), USERS_FILE_NAME);
    if ((fp = fopen(fname, "r")) == NULL)
    {
        return NULL;
    }
    if ((rval = users_alloc()) == NULL)
    {
        fclose(fp);
        return NULL;
    }
    while (fgets(uname, sizeof(uname), fp))
    {
        char *nl = strchr(uname, '\n');

        if (nl)
        {
            *nl = '\0';
        }
        else if (!feof(fp))
        {
            MXS_ERROR("Line length exceeds %d characters, possible corrupted "
                      "'passwd' file in: %s", LINELEN, fname);
            users_free(rval);
            rval = NULL;
            break;
        }

        char *tmp_ptr = strchr(uname, ':');
        if (tmp_ptr)
        {
            *tmp_ptr = '\0';
            MXS_WARNING("Found user '%s' with password. "
                        "This user might not be compatible with new maxadmin in MaxScale 2.0. "
                        "Remove it with \"remove user %s\" through MaxAdmin", uname, uname);
        }
        if (users_add(rval, uname, ""))
        {
            added_users++;
        }
    }
    fclose(fp);

    if (!added_users)
    {
        users_free(rval);
        rval = NULL;
    }

    return rval;
}

/**
 * Add user
 *
 * @param uname         Name of the new user
 * @return      NULL on success or an error string on failure
 */
char *
admin_add_user(char *uname)
{
    FILE *fp;
    char fname[PATH_MAX], *home;

    initialise();

    if (access(get_datadir(), F_OK) != 0)
    {
        if (mkdir(get_datadir(), S_IRWXU) != 0 && errno != EEXIST)
        {
            return ADMIN_ERR_PWDFILEOPEN;
        }
    }

    snprintf(fname, sizeof(fname), "%s/%s", get_datadir(), USERS_FILE_NAME);
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
    users_add(users, uname, "");
    if ((fp = fopen(fname, "a")) == NULL)
    {
        MXS_ERROR("Unable to append to password file %s.", fname);
        return ADMIN_ERR_FILEAPPEND;
    }
    fprintf(fp, "%s\n", uname);
    fclose(fp);
    return ADMIN_SUCCESS;
}


/**
 * Remove maxscale user from in-memory structure and from password file
 *
 * @param uname         Name of the new user
 * @return      NULL on success or an error string on failure
 */
char* admin_remove_user(
    char* uname)
{
    FILE*  fp;
    FILE*  fp_tmp;
    char   fname[PATH_MAX];
    char   fname_tmp[PATH_MAX];
    char*  home;
    char   fusr[LINELEN];
    char   fpwd[LINELEN];
    char   line[LINELEN];
    fpos_t rpos;

    if (strcmp(uname, DEFAULT_ADMIN_USER) == 0)
    {
        MXS_WARNING("Attempt to delete the default admin user '%s'.", uname);
        return ADMIN_ERR_DELROOT;
    }

    if (!admin_search_user(uname))
    {
        MXS_ERROR("Couldn't find user %s. Removing user failed.", uname);
        return ADMIN_ERR_USERNOTFOUND;
    }

    /** Remove user from in-memory structure */
    users_delete(users, uname);

    /**
     * Open passwd file and remove user from the file.
     */
    snprintf(fname, sizeof(fname), "%s/%s", get_datadir(), USERS_FILE_NAME);
    snprintf(fname_tmp, sizeof(fname_tmp), "%s/%s_tmp", get_datadir(), USERS_FILE_NAME);
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

    while (fgets(fusr, sizeof(fusr), fp))
    {
        char *nl = strchr(fusr, '\n');

        if (nl)
        {
            *nl = '\0';
        }
        else if (!feof(fp))
        {
            MXS_ERROR("Line length exceeds %d characters, possible corrupted "
                      "'passwd' file in: %s", LINELEN, fname);
            fclose(fp);
            fclose(fp_tmp);
            return ADMIN_ERR_PWDFILEACCESS;
        }

        /**
         * Compare username what was found from passwd file.
         * Unmatching lines are copied to tmp file.
         */
        if (strncmp(uname, fusr, strlen(uname) + 1) != 0)
        {
            if (fsetpos(fp, &rpos) != 0)
            {
                /** one step back */
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

    int rv = 0;

    if (strcmp(user, DEFAULT_ADMIN_USER) == 0)
    {
        rv = 1;
    }
    else if (users)
    {
        rv = (users_fetch(users, user) != NULL);
    }

    return rv;
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
