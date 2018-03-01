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
#include <maxscale/cdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <crypt.h>
#include <maxscale/users.h>
#include <maxscale/adminusers.h>
#include <maxscale/log_manager.h>
#include <maxscale/paths.h>
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
 * 02/09/16     Johan Wikman         Enabled Linux accounts and MaxScale users
 *
 * @endverbatim
 */
static void  initialise();

static USERS *loadLinuxUsers();
static USERS *loadInetUsers();

static const char *admin_add_user(USERS** pusers, const char* fname,
                                  const char* uname, const char* password);
static const char* admin_remove_user(USERS *users, const char* fname,
                                     const char *uname, const char *passwd);
static bool admin_search_user(USERS *users, const char *uname);



static USERS *linux_users = NULL;
static USERS *inet_users = NULL;
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

static const char LINUX_USERS_FILE_NAME[] = "maxadmin-users";
static const char INET_USERS_FILE_NAME[]  = "passwd";

static const char INET_DEFAULT_USERNAME[] = "admin";
static const char INET_DEFAULT_PASSWORD[] = "mariadb";

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
    linux_users = loadLinuxUsers();
    inet_users = loadInetUsers();
}

static const char *admin_add_user(USERS** pusers, const char* fname,
                                  const char* uname, const char* password)
{
    FILE *fp;
    char path[PATH_MAX], *home;

    if (access(get_datadir(), F_OK) != 0)
    {
        if (mkdir(get_datadir(), S_IRWXU) != 0 && errno != EEXIST)
        {
            return ADMIN_ERR_PWDFILEOPEN;
        }
    }

    snprintf(path, sizeof(path), "%s/%s", get_datadir(), fname);
    if (*pusers == NULL)
    {
        MXS_NOTICE("Create initial password file.");

        if ((*pusers = users_alloc()) == NULL)
        {
            return ADMIN_ERR_NOMEM;
        }
        if ((fp = fopen(path, "w")) == NULL)
        {
            MXS_ERROR("Unable to create password file %s.", path);
            return ADMIN_ERR_PWDFILEOPEN;
        }
        fclose(fp);
    }
    if (users_fetch(*pusers, (char*)uname) != NULL) // TODO: Make users const correct.
    {
        return ADMIN_ERR_DUPLICATE;
    }
    users_add(*pusers, (char*)uname, password ? (char*)password : ""); // TODO: Make users const correct.
    if ((fp = fopen(path, "a")) == NULL)
    {
        MXS_ERROR("Unable to append to password file %s.", path);
        return ADMIN_ERR_FILEAPPEND;
    }
    if (password)
    {
        fprintf(fp, "%s:%s\n", uname, password);
    }
    else
    {
        fprintf(fp, "%s\n", uname);
    }
    fclose(fp);
    return ADMIN_SUCCESS;
}

static const char* admin_remove_user(USERS *users, const char* fname,
                                     const char *uname, const char *passwd)
{
    FILE*  fp;
    FILE*  fp_tmp;
    char   path[PATH_MAX];
    char   path_tmp[PATH_MAX];
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

    if (!admin_search_user(users, uname))
    {
        MXS_ERROR("Couldn't find user %s. Removing user failed.", uname);
        return ADMIN_ERR_USERNOTFOUND;
    }

    if (passwd)
    {
        if (admin_verify_inet_user(uname, passwd) == 0)
        {
            MXS_ERROR("Authentication failed, wrong user/password "
                      "combination. Removing user failed.");
            return ADMIN_ERR_AUTHENTICATION;
        }
    }

    /** Remove user from in-memory structure */
    users_delete(users, (char*)uname); // TODO: Make users const correct.

    /**
     * Open passwd file and remove user from the file.
     */
    snprintf(path, sizeof(path), "%s/%s", get_datadir(), fname);
    snprintf(path_tmp, sizeof(path_tmp), "%s/%s_tmp", get_datadir(), fname);
    /**
     * Rewrite passwd file from memory.
     */
    if ((fp = fopen(path, "r")) == NULL)
    {
        int err = errno;
        MXS_ERROR("Unable to open password file %s : errno %d.\n"
                  "Removing user from file failed; it must be done "
                  "manually.",
                  path,
                  err);
        return ADMIN_ERR_PWDFILEOPEN;
    }
    /**
     * Open temporary passwd file.
     */
    if ((fp_tmp = fopen(path_tmp, "w")) == NULL)
    {
        int err = errno;
        MXS_ERROR("Unable to open tmp file %s : errno %d.\n"
                  "Removing user from passwd file failed; it must be done "
                  "manually.",
                  path_tmp,
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
                  path,
                  err);
        fclose(fp);
        fclose(fp_tmp);
        unlink(path_tmp);
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
                      "'passwd' file in: %s", LINELEN, path);
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
            if (fgets(line, LINELEN, fp))
            {
                fputs(line, fp_tmp);
            }
            else
            {
                MXS_ERROR("Failed to read line from admin users file");
            }
        }

        if (fgetpos(fp, &rpos) != 0)
        {
            int err = errno;
            MXS_ERROR("Unable to process passwd file %s : "
                      "errno %d.\n"
                      "Removing user from file failed, and must be "
                      "done manually.",
                      path,
                      err);
            fclose(fp);
            fclose(fp_tmp);
            unlink(path_tmp);
            return ADMIN_ERR_PWDFILEACCESS;
        }
    }
    fclose(fp);
    /**
     * Replace original passwd file with new.
     */
    if (rename(path_tmp, path))
    {
        int err = errno;
        MXS_ERROR("Unable to rename new passwd file %s : errno "
                  "%d.\n"
                  "Rename it to %s manually.",
                  path_tmp,
                  err,
                  path);
        unlink(path_tmp);
        fclose(fp_tmp);
        return ADMIN_ERR_PWDFILEACCESS;
    }
    fclose(fp_tmp);
    return ADMIN_SUCCESS;
}

/**
 * Check for existance of the user
 *
 * @param uname The user name to test
 * @return      True if the user exists
 */
static bool admin_search_user(USERS *users, const char *uname)
{
    return (users_fetch(users, (char*)uname) != NULL); // TODO: Make users const correct.
}

/**
 */
void dcb_print_users(DCB *dcb, const char* heading, USERS *users)
{
    dcb_printf(dcb, "%s", heading);

    if (users)
    {
        HASHITERATOR *iter = hashtable_iterator(users->data);

        if (iter)
        {
            char *sep = "";
            const char *user;

            while ((user = hashtable_next(iter)) != NULL)
            {
                dcb_printf(dcb, "%s%s", sep, user);
                sep = ", ";
            }

            hashtable_iterator_free(iter);
        }
    }

    dcb_printf(dcb, "%s", "\n");
}

/**
 * Load the admin users
 *
 * @return Table of users
 */
static USERS *
loadUsers(const char *fname)
{
    USERS *rval;
    FILE  *fp;
    char  path[PATH_MAX], *home;
    char  uname[80];
    int added_users = 0;

    initialise();
    snprintf(path, sizeof(path), "%s/%s", get_datadir(), fname);
    if ((fp = fopen(path, "r")) == NULL)
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
            MXS_ERROR("Line length exceeds %d characters, possibly corrupted "
                      "'passwd' file in: %s", LINELEN, path);
            users_free(rval);
            rval = NULL;
            break;
        }

        char *password;
        char *colon = strchr(uname, ':');
        if (colon)
        {
            // Inet case
            *colon = 0;
            password = colon + 1;
        }
        else
        {
            // Linux case.
            password = "";
        }

        if (users_add(rval, uname, password))
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


static USERS *loadLinuxUsers()
{
    return loadUsers(LINUX_USERS_FILE_NAME);
}

static USERS *loadInetUsers()
{
    return loadUsers(INET_USERS_FILE_NAME);
}

/**
 * Enable Linux account
 *
 * @param uname Name of Linux user
 *
 * @return NULL on success or an error string on failure.
 */
const char *admin_enable_linux_account(const char *uname)
{
    initialise();

    return admin_add_user(&linux_users, LINUX_USERS_FILE_NAME, uname, NULL);
}

/**
 * Disable Linux account
 *
 * @param uname Name of Linux user
 *
 * @return NULL on success or an error string on failure.
 */
const char* admin_disable_linux_account(const char* uname)
{
    initialise();

    return admin_remove_user(linux_users, LINUX_USERS_FILE_NAME, uname, NULL);
}

/**
 * Check whether Linux account is enabled
 *
 * @param uname The user name
 *
 * @return True if the account is enabled, false otherwise.
 */
bool admin_linux_account_enabled(const char *uname)
{
    initialise();

    bool rv = false;

    if (strcmp(uname, DEFAULT_ADMIN_USER) == 0)
    {
        rv = true;
    }
    else if (linux_users)
    {
        rv = admin_search_user(linux_users, uname);
    }

    return rv;
}

/**
 * Add insecure remote (network) user.
 *
 * @param uname    Name of the new user.
 * @param password Password of the new user.
 *
 * @return NULL on success or an error string on failure.
 */
const char *admin_add_inet_user(const char *uname, const char* password)
{
    initialise();

    struct crypt_data cdata;
    cdata.initialized = 0;
    char *cpassword = crypt_r(password, ADMIN_SALT, &cdata);

    return admin_add_user(&inet_users, INET_USERS_FILE_NAME, uname, cpassword);
}

/**
 * Remove insecure remote (network) user
 *
 * @param uname    Name of user to be removed.
 * @param password Password of user to be removed.
 *
 * @return NULL on success or an error string on failure.
 */
const char* admin_remove_inet_user(const char* uname, const char *password)
{
    initialise();

    return admin_remove_user(inet_users, INET_USERS_FILE_NAME, uname, password);
}

/**
 * Check for existance of remote user.
 *
 * @param user The user name to test.
 *
 * @return True if the user exists, false otherwise.
 */
bool admin_inet_user_exists(const char *uname)
{
    initialise();

    bool rv = false;

    if (inet_users)
    {
        rv = admin_search_user(inet_users, uname);
    }

    return rv;
}

/**
 * Verify a remote user name and password
 *
 * @param username  Username to verify
 * @param password  Password to verify
 *
 * @return True if the username/password combination is valid
 */
bool
admin_verify_inet_user(const char *username, const char *password)
{
    bool rv = false;

    initialise();

    if (inet_users)
    {
        const char* pw = users_fetch(inet_users, (char*)username); // TODO: Make users const-correct.

        if (pw)
        {
            struct crypt_data cdata;
            cdata.initialized = 0;
            if (strcmp(pw, crypt_r(password, ADMIN_SALT, &cdata)) == 0)
            {
                rv = true;
            }
        }
    }
    else
    {
        if (strcmp(username, INET_DEFAULT_USERNAME) == 0
            && strcmp(password, INET_DEFAULT_PASSWORD) == 0)
        {
            rv = true;
        }
    }

    return rv;
}

/**
 * Print Linux and and inet users
 *
 * @param dcb A DCB to send the output to.
 */
void dcb_PrintAdminUsers(DCB *dcb)
{
    dcb_print_users(dcb, "Enabled Linux accounts (secure)    : ", linux_users);
    dcb_print_users(dcb, "Created network accounts (insecure): ", inet_users);
}
